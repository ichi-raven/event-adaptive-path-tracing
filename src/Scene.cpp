/*****************************************************************/ /**
 * @file   Scene.cpp
 * @brief  source file of scene class
 * 
 * @author ichi-raven
 * @date   June 2024
 *********************************************************************/

#define GLM_ENABLE_EXPERIMENTAL

#include "../include/Scene.hpp"
#include "../include/AnimatedTransform.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include <tinyexr.h>

#include <cmath>
#include <iostream>
#include <regex>
#include <set>
#include <algorithm>
#include <stdexcept>

#include <ng-log/logging.h>

namespace
{
    inline glm::quat convert(const aiQuaternion& from)
    {
        glm::quat to{};
        to.w = from.w;
        to.x = from.x;
        to.y = from.y;
        to.z = from.z;
        return to;
    }

    inline glm::vec3 convert(const aiVector3D& from)
    {
        glm::vec3 to{};
        to.x = from.x;
        to.y = from.y;
        to.z = from.z;
        return to;
    }

    inline glm::vec4 convert(const aiColor4D& from)
    {
        glm::vec4 to{};
        to.x = from.r;
        to.y = from.g;
        to.z = from.b;
        to.w = from.a;
        return to;
    }

    Handle<vk2s::Image> loadExr(vk2s::Device& device, std::string_view path)
    {
        float* rgba;  // width * height * RGBA
        int width;
        int height;
        const char* err = nullptr;  // or nullptr in C++11

        int ret = LoadEXR(&rgba, &width, &height, path.data(), &err);

        if (ret != TINYEXR_SUCCESS)
        {
            if (err)
            {
                assert(!"failed to load exr file!");
                FreeEXRErrorMessage(err);  // release memory of error message.
                return Handle<vk2s::Image>();
            }
        }
        else
        {
            vk::Extent3D extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1);
            vk::Format format = vk::Format::eR32G32B32A32Sfloat;
            const size_t size = extent.width * extent.height * 4 * sizeof(float);

            Handle<vk2s::Image> rtnImage;
            {
                vk::ImageCreateInfo ci;
                ci.arrayLayers   = 1;
                ci.extent        = extent;
                ci.format        = format;
                ci.imageType     = vk::ImageType::e2D;
                ci.mipLevels     = 1;
                ci.usage         = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
                ci.initialLayout = vk::ImageLayout::eUndefined;

                vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

                rtnImage = device.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size,
                                                      vk::ImageViewType::e2D, range);
            }

            rtnImage->write(reinterpret_cast<std::byte*>(rgba), size);
            free(rgba);  // release memory of image data
            return rtnImage;
        }

        // invalid
        return Handle<vk2s::Image>();
    }

}  // namespace

namespace evr
{
    Scene::Scene(vk2s::Device& device, const Options& options)
        : mDirectory(options.input.substr(0, options.input.find_last_of("/\\")))
        , mPath(options.input)
        , mDevice(device)
        , mOptions(options)
        , mCameraExists(false)
    {
        const aiScene* pScene =
            mImporter.ReadFile(mPath.data(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

        if (!pScene || pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pScene->mRootNode)
        {
            std::cerr << "ERROR::ASSIMP::" << mImporter.GetErrorString() << "\n";
            assert(0);
            return;
        }

        // before traversaling mesh nodes, search envmap first
        if (options.infiniteEmitterPath.empty())
        {
            // auto& infiniteEmitter           = mEmitters.emplace_back();
            // infiniteEmitter.params.type     = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eInfinite);
            // infiniteEmitter.params.texIndex = 0;  // ensure that the image for the infinite light source is always at the front

            for (unsigned int i = 0; i < pScene->mNumMaterials; i++)
            {
                aiMaterial* pMat = pScene->mMaterials[i];

                aiString texturePath;
                if (AI_SUCCESS == pMat->GetTexture(aiTextureType_AMBIENT, 0, &texturePath))
                {
                    auto& infiniteEmitter = mEmitters.emplace_back();
                    infiniteEmitter.params.type =
                        static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eInfinite);

                    // NOTE: ensure that the image for the infinite light source is always at the front
                    infiniteEmitter.params.texIndex = 0;

                    mTextures.emplace_back(loadTextureImage(pScene, texturePath, vk::Format::eR8G8B8A8Srgb));
                    break;
                }
            }
        }
        else
        {
            auto& infiniteEmitter       = mEmitters.emplace_back();
            infiniteEmitter.params.type = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eInfinite);
            infiniteEmitter.params.texIndex =
                0;  // ensure that the image for the infinite light source is always at the front
            mTextures.emplace_back(loadExr(mDevice, options.infiniteEmitterPath));
            // TODO: create pdf map?
        }

        // load camera
        loadCamera(pScene, options);

        // prepare vectors
        mMeshes.reserve(pScene->mNumMeshes);
        mMaterials.reserve(pScene->mNumMaterials);
        mAnimTransforms.reserve(pScene->mNumMeshes);

        // load all mesh / materials
        processNode(pScene, pScene->mRootNode);

        // build BLAS, TLAS and transition texture images together
        build();
    }

    Scene::~Scene()
    {
        for (auto& mesh : mMeshes)
        {
            mDevice.destroy(mesh.blas);
            mDevice.destroy(mesh.vertexBuffer);
            mDevice.destroy(mesh.indexBuffer);
            mDevice.destroy(mesh.instanceBuffer);
        }

        for (auto& texture : mTextures)
        {
            mDevice.destroy(texture);
        }
    }

    const std::vector<Mesh>& Scene::getMeshes() const
    {
        return mMeshes;
    }

    const std::vector<Material>& Scene::getMaterials() const
    {
        return mMaterials;
    }

    const std::vector<Handle<vk2s::Image>>& Scene::getTextures() const
    {
        return mTextures;
    }

    const std::vector<Emitter>& Scene::getEmitters() const
    {
        return mEmitters;
    }

    const vk2s::Camera& Scene::getCamera() const
    {
        return mCamera;
    }

    vk2s::Camera& Scene::getCameraRef()
    {
        return mCamera;
    }

    const Handle<vk2s::AccelerationStructure>& Scene::getTLAS(const uint32_t frameIndex) const
    {
        assert(frameIndex < mTLASs.size());
        return mTLASs[frameIndex];
    }

    // const Handle<vk2s::AccelerationStructure>& Scene::getTLAS(const double t0, const double t1) const
    // {
    //     const uint32_t frameIndex = static_cast<uint32_t>(t1);
    //     assert(frameIndex < mTLASs.size());
    //     return mTLASs[frameIndex];
    // }

    const std::vector<AnimatedTransform>& Scene::getAnimatedTransforms() const
    {
        return mAnimTransforms;
    }

    void Scene::update(const double t)
    {
        if (mCameraExists)
        {
            const auto& trans = mCameraAnimTransform.mix(t);
            mCamera.setPos(trans.pos);

            const glm::vec3 rotatedZ = trans.rot * glm::vec3(0.f, 0.f, 1.f);
            mCamera.setTheta(acos(rotatedZ.y));
            mCamera.setPhi(atan2(rotatedZ.z, rotatedZ.x) + glm::half_pi<float>());
        }
    }

    void Scene::processNode(const aiScene* pScene, const aiNode* pNode, std::shared_ptr<AnimatedTransform> parent)
    {
        auto localTransform    = std::make_shared<AnimatedTransform>();
        localTransform->parent = parent;

        const bool hasAnimation = loadAnimatedTransform(pScene, pNode, *localTransform);

        for (uint32_t i = 0; i < pNode->mNumMeshes; ++i)
        {
            aiMesh* pMesh = pScene->mMeshes[pNode->mMeshes[i]];
            if (pMesh)
            {
                if (!processMesh(pScene, pMesh, *localTransform, hasAnimation))
                {
                    LOG(WARNING) << "failed to load mesh!\n";
                    continue;
                }
            }
        }

        for (uint32_t i = 0; i < pNode->mNumChildren; ++i)
        {
            processNode(pScene, pNode->mChildren[i], localTransform);
        }
    }

    bool Scene::processMesh(const aiScene* pScene, const aiMesh* pMesh, const AnimatedTransform& animTransform,
                            [[maybe_unused]] bool hasAnimation)
    {
        using Vertex = Mesh::Vertex;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.resize(pMesh->mNumVertices);

        // walk through each of the mesh's vertices
        for (uint32_t i = 0; i < pMesh->mNumVertices; ++i)
        {
            auto& vertex = vertices[i];

            vertex.pos = convert(pMesh->mVertices[i]);

            if (pMesh->mNormals)
            {
                vertex.normal = convert(pMesh->mNormals[i]);
            }
            else
            {
                vertex.normal = glm::vec3(0);
            }

            if (pMesh->mTextureCoords[0])
            {
                vertex.u = static_cast<float>(pMesh->mTextureCoords[0][i].x);
                vertex.v = static_cast<float>(pMesh->mTextureCoords[0][i].y);
            }
            else
            {
                vertex.u = 0;
                vertex.v = 0;
            }

            if (pMesh->mNormals && pMesh->mTangents && pMesh->mBitangents)
            {
                const glm::vec3 n = convert(pMesh->mNormals[i]);
                const glm::vec3 t = convert(pMesh->mTangents[i]);
                const glm::vec3 b = convert(pMesh->mBitangents[i]);
                float handedness  = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
                vertex.tangent    = glm::vec4(t, handedness);
            }
            else
            {
                vertex.tangent = glm::vec4(0.0f);
            }
        }

        if (pMesh->mFaces)
        {
            // WARN: only for triangulated mesh
            assert(pMesh->mFaces[0].mNumIndices == 3);
            indices.resize(pMesh->mNumFaces * pMesh->mFaces[0].mNumIndices);

            for (uint32_t i = 0; i < pMesh->mNumFaces; ++i)
            {
                aiFace face = pMesh->mFaces[i];
                for (uint32_t j = 0; j < face.mNumIndices; ++j)
                {
                    indices[i * face.mNumIndices + j] = face.mIndices[j];
                }
            }
        }

        // start to write mesh data
        auto& mesh = mMeshes.emplace_back();

        // load and add material
        auto [matIdx, hasEmissive] = addMaterial(pScene, pMesh);

        // if the material has emissive component, add an emitter
        if (hasEmissive)
        {
            mEmitters.reserve(mEmitters.size() + pMesh->mNumFaces);

            for (uint32_t primitiveIndex = 0; primitiveIndex < pMesh->mNumFaces; ++primitiveIndex)
            {
                auto& emitter = mEmitters.emplace_back();

                emitter.params.emissive  = mMaterials[matIdx].params.emissive;
                emitter.params.type      = static_cast<std::underlying_type_t<Emitter::Type>>(Emitter::Type::eArea);
                emitter.params.faceNum   = pMesh->mNumFaces;
                emitter.params.meshIndex = mMeshes.size() - 1;
                emitter.params.primitiveIndex = primitiveIndex;
            }
        }

        {  // vertex buffer
            const auto vbSize  = vertices.size() * sizeof(Vertex);
            const auto vbUsage = vk::BufferUsageFlagBits::eVertexBuffer |
                                 vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                 vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst |
                                 vk::BufferUsageFlagBits::eStorageBuffer;
            vk::BufferCreateInfo ci({}, vbSize, vbUsage);
            vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eDeviceLocal;

            mesh.vertexBuffer = mDevice.create<vk2s::Buffer>(ci, fb);
            mesh.vertexBuffer->upload(vertices.data(), vbSize);
        }

        {  // index buffer
            const auto ibSize  = indices.size() * sizeof(uint32_t);
            const auto ibUsage = vk::BufferUsageFlagBits::eIndexBuffer |
                                 vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                 vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst |
                                 vk::BufferUsageFlagBits::eStorageBuffer;

            vk::BufferCreateInfo ci({}, ibSize, ibUsage);
            vk::MemoryPropertyFlags fb = vk::MemoryPropertyFlagBits::eDeviceLocal;

            mesh.indexBuffer = mDevice.create<vk2s::Buffer>(ci, fb);
            mesh.indexBuffer->upload(indices.data(), ibSize);
        }

        // BLAS is built collectively at the end

        // load animation if exists
        mAnimTransforms.push_back(animTransform);

        return true;
    }

    std::pair<uint32_t, bool> Scene::addMaterial(const aiScene* pScene, const aiMesh* pMesh)
    {
        Material& material           = mMaterials.emplace_back();
        material.params.materialType = static_cast<std::underlying_type_t<Material::Type>>(Material::Type::ePrinciple);

        bool hasEmissive = false;
        if (pMesh->mMaterialIndex >= 0 && pMesh->mMaterialIndex < pScene->mNumMaterials)
        {
            aiMaterial* pMat = pScene->mMaterials[pMesh->mMaterialIndex];

            material.params.albedoTexIndex =
                loadMaterialTexture(pScene, pMat, aiTextureType::aiTextureType_DIFFUSE, vk::Format::eR8G8B8A8Srgb);
            material.params.roughnessTexIndex = loadMaterialTexture(
                pScene, pMat, aiTextureType::aiTextureType_DIFFUSE_ROUGHNESS, vk::Format::eR8G8B8A8Unorm);
            material.params.metalnessTexIndex =
                loadMaterialTexture(pScene, pMat, aiTextureType::aiTextureType_METALNESS, vk::Format::eR8G8B8A8Unorm);
            material.params.normalMapTexIndex =
                loadMaterialTexture(pScene, pMat, aiTextureType::aiTextureType_NORMALS, vk::Format::eR8G8B8A8Unorm);

            if (material.params.albedoTexIndex != Material::Params::kInvalidTexIndex)
            {
                material.params.albedo = glm::vec3(1.0f);
            }

            if (material.params.roughnessTexIndex != Material::Params::kInvalidTexIndex)
            {
                material.params.roughness = 1.0f;
            }

            if (material.params.metalnessTexIndex != Material::Params::kInvalidTexIndex)
            {
                material.params.metallic = 1.0f;
            }

            aiColor4D color;
            if (AI_SUCCESS == aiGetMaterialColor(pMat, AI_MATKEY_COLOR_DIFFUSE, &color))
            {
                material.params.albedo = glm::vec3(convert(color));
            }

            float metallicFactor = 0.f;
            if (AI_SUCCESS == aiGetMaterialFloat(pMat, AI_MATKEY_METALLIC_FACTOR, &metallicFactor))
            {
                material.params.metallic = metallicFactor;
            }

            if (AI_SUCCESS == aiGetMaterialColor(pMat, AI_MATKEY_COLOR_EMISSIVE, &color))
            {
                material.params.emissive = convert(color);
                float emissiveIntensity  = 1.f;
                if (AI_SUCCESS == aiGetMaterialFloat(pMat, AI_MATKEY_EMISSIVE_INTENSITY, &emissiveIntensity))
                {
                    material.params.emissive *= emissiveIntensity;
                }

                hasEmissive = !color.IsBlack();
            }

            float Ns        = 0.f;
            int iNs         = 0;
            float roughness = 0.f;

            if (AI_SUCCESS == aiGetMaterialFloat(pMat, AI_MATKEY_SHININESS, &Ns))
            {
                material.params.roughness = (1000.f - Ns) / 1000.f;
            }
            else if (AI_SUCCESS == aiGetMaterialInteger(pMat, AI_MATKEY_SHININESS, &iNs))
            {
                material.params.roughness = (1000.f - static_cast<float>(iNs)) / 1000.f;
            }

            if (AI_SUCCESS == aiGetMaterialFloat(pMat, AI_MATKEY_ROUGHNESS_FACTOR, &roughness))
            {
                material.params.roughness = roughness;
            }

            float transmission = 0.f;
            if (AI_SUCCESS == aiGetMaterialFloat(pMat, AI_MATKEY_TRANSMISSION_FACTOR, &transmission))
            {
                material.params.specTrans = transmission;
            }

            float IOR = 1.5f;
            if (AI_SUCCESS == aiGetMaterialFloat(pMat, AI_MATKEY_REFRACTI, &IOR))
            {
                material.params.IOR = IOR;
            }
            else
            {
                material.params.IOR = 1.5f;
            }
        }

        const auto matIdx = static_cast<uint32_t>(mMaterials.size() - 1);

        return { matIdx, hasEmissive };
    }

    uint32_t Scene::loadMaterialTexture(const aiScene* pScene, const aiMaterial* mat, const aiTextureType aiType,
                                        vk::Format format)
    {
        aiString str;
        mat->GetTexture(aiType, 0, &str);

        if (str.Empty())
        {
            return Material::Params::kInvalidTexIndex;
        }

        const auto key  = std::make_tuple(std::string(str.C_Str()), format);
        const auto& itr = mTextureMap.find(key);
        if (itr != mTextureMap.end())
        {
            return itr->second;
        }

        {  // If texture hasn't been loaded already, load it
            auto texture = loadTextureImage(pScene, str, format);
            mTextures.emplace_back(texture);

            return mTextureMap.emplace(key, mTextures.size() - 1).first->second;
        }

        return Material::Params::kInvalidTexIndex;
    }

    Handle<vk2s::Image> Scene::loadTextureImage(const aiScene* pScene, const aiString& texturePath, vk::Format format)
    {
        Handle<vk2s::Image> texture;
        const aiTexture* embeddedTexture = pScene->GetEmbeddedTexture(texturePath.C_Str());

        if (embeddedTexture != nullptr)
        {
            // create texture from embedded data
            auto width               = embeddedTexture->mWidth;
            auto height              = embeddedTexture->mHeight;
            void* pData              = embeddedTexture->pcData;
            bool decompressedWithStb = false;

            if (height == 0)
            {
                int x = 0, y = 0, channelNum = 0;
                pData = static_cast<void*>(stbi_load_from_memory(reinterpret_cast<stbi_uc*>(embeddedTexture->pcData),
                                                                 width, &x, &y, &channelNum, STBI_rgb_alpha));
                assert(pData && x != 0 && y != 0 && channelNum != 0);
                width               = x;
                height              = y;
                decompressedWithStb = true;
            }

            const uint32_t size = width * height * vk2s::Compiler::getSizeOfFormat(format);

            vk::ImageCreateInfo ci;
            ci.arrayLayers   = 1;
            ci.extent        = vk::Extent3D(width, height, 1);
            ci.format        = format;
            ci.imageType     = vk::ImageType::e2D;
            ci.mipLevels     = 1;
            ci.usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            ci.initialLayout = vk::ImageLayout::eUndefined;

            // change format to pooling
            texture = mDevice.create<vk2s::Image>(ci, vk::MemoryPropertyFlagBits::eDeviceLocal, size,
                                                  vk::ImageAspectFlagBits::eColor);
            texture->write(reinterpret_cast<std::byte*>(pData), size);

            if (decompressedWithStb)
            {
                stbi_image_free(pData);
            }
        }
        else
        {
            std::string filename = std::regex_replace(texturePath.C_Str(), std::regex("\\\\"), "/");
            filename             = mDirectory + '/' + filename;

            texture = mDevice.create<vk2s::Image>(filename, format);
        }

        return texture;
    }

    Handle<vk2s::AccelerationStructure> Scene::buildTLAS(const std::pair<double, double> t,
                                                         Handle<vk2s::Command> command)
    {
        // deploy instances
        vk::AccelerationStructureInstanceKHR templateDesc{};
        templateDesc.instanceCustomIndex = 0;
        templateDesc.mask                = 0xFF;
        templateDesc.flags               = 0;

        std::vector<vk2s::AccelerationStructure::MotionInstancePadNV> asInstances;
        //std::vector<vk::AccelerationStructureInstanceKHR> asInstances;

        asInstances.reserve(mMeshes.size());

        const auto appendInstance = [&](const Mesh& mesh, const AnimatedTransform& animTransform)
        {
            const auto [t0, t1] = t;
            const auto& blas    = mesh.blas;
            auto delta          = animTransform.getWorld(t0, t1);

            // One key means that the transform is static, so we can use a static instance
            if (animTransform.isStatic())
            {
                vk::AccelerationStructureInstanceKHR asInstance{};
                asInstance.transform                              = delta.t0.params.convert();
                asInstance.accelerationStructureReference         = blas->getVkDeviceAddress();
                asInstance.instanceShaderBindingTableRecordOffset = 0;
                asInstance.flags = static_cast<uint32_t>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
                asInstance.mask  = templateDesc.mask;
                asInstance.instanceCustomIndex = 0;

                vk2s::AccelerationStructure::MotionInstancePadNV pad{};
                pad.type                = vk::AccelerationStructureMotionInstanceTypeNV::eStatic;
                pad.data.staticInstance = asInstance;

                asInstances.emplace_back(pad);
                return;
            }

            VkSRTDataNV t0SRT{};
            {
                t0SRT.qw = delta.t0.rot.w;
                t0SRT.qx = delta.t0.rot.x;
                t0SRT.qy = delta.t0.rot.y;
                t0SRT.qz = delta.t0.rot.z;

                t0SRT.sx = delta.t0.scale.x;
                t0SRT.sy = delta.t0.scale.y;
                t0SRT.sz = delta.t0.scale.z;

                t0SRT.a = delta.t0.shear.x;
                t0SRT.b = delta.t0.shear.y;
                t0SRT.c = delta.t0.shear.z;

                t0SRT.tx = delta.t0.pos.x;
                t0SRT.ty = delta.t0.pos.y;
                t0SRT.tz = delta.t0.pos.z;
            }

            VkSRTDataNV t1SRT{};
            {
                t1SRT.qw = delta.t1.rot.w;
                t1SRT.qx = delta.t1.rot.x;
                t1SRT.qy = delta.t1.rot.y;
                t1SRT.qz = delta.t1.rot.z;

                t1SRT.sx = delta.t1.scale.x;
                t1SRT.sy = delta.t1.scale.y;
                t1SRT.sz = delta.t1.scale.z;

                t1SRT.a = delta.t1.shear.x;
                t1SRT.b = delta.t1.shear.y;
                t1SRT.c = delta.t1.shear.z;

                t1SRT.tx = delta.t1.pos.x;
                t1SRT.ty = delta.t1.pos.y;
                t1SRT.tz = delta.t1.pos.z;
            }

            vk::AccelerationStructureSRTMotionInstanceNV asInstance{};
            asInstance.accelerationStructureReference         = blas->getVkDeviceAddress();
            asInstance.instanceShaderBindingTableRecordOffset = 0;
            asInstance.transformT0                            = t0SRT;
            asInstance.transformT1                            = t1SRT;
            asInstance.flags = static_cast<uint32_t>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            asInstance.mask  = templateDesc.mask;
            asInstance.instanceCustomIndex = 0;

            vk2s::AccelerationStructure::MotionInstancePadNV pad{};
            pad.type = vk::AccelerationStructureMotionInstanceTypeNV::eSrtMotion;
            pad.data = asInstance;

            asInstances.emplace_back(pad);
        };

        assert(mAnimTransforms.size() == mMeshes.size());

        for (size_t i = 0; i < mMeshes.size(); ++i)
        {
            appendInstance(mMeshes[i], mAnimTransforms[i]);
        }

        return mDevice.create<vk2s::AccelerationStructure>(asInstances, command);
    }

    void Scene::build()
    {
        auto buildCmd                   = mDevice.createUnique<vk2s::Command>();
        UniqueHandle<vk2s::Fence> fence = mDevice.createUnique<vk2s::Fence>(false);
        fence->reset();

        buildCmd->begin();

        // BLAS
        for (auto& mesh : mMeshes)
        {
            const uint32_t vertNum = mesh.vertexBuffer->getSize() / sizeof(Mesh::Vertex);
            const uint32_t faceNum = mesh.indexBuffer->getSize() / sizeof(uint32_t) / 3;
            const bool motion      = false;  //TODO: false for static mesh
            mesh.blas =
                mDevice.create<vk2s::AccelerationStructure>(vertNum, sizeof(Mesh::Vertex), mesh.vertexBuffer.get(),
                                                            faceNum, mesh.indexBuffer.get(), motion, buildCmd);
        }

        buildCmd->end();
        buildCmd->execute(fence);
        fence->wait();
        fence->reset();

        // Build all TLASs from animated transforms in one submission
        mTLASs.reserve(*mOptions.frames);

        buildCmd->reset();
        buildCmd->begin(true);

        // TLAS
        for (uint32_t frameIndex = 0; frameIndex < *mOptions.frames; ++frameIndex)
        {
            const auto t0 = *mOptions.deltaTime * frameIndex;
            const auto t1 = t0 + *mOptions.deltaTime;

            mTLASs.emplace_back(buildTLAS({ t0, t1 }, buildCmd));
        }

        // Make the completed AS visible to ray tracing
        vk::MemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
        barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR;

        buildCmd->globalPipelineBarrier(barrier, vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                                        vk::PipelineStageFlagBits::eRayTracingShaderKHR);

        buildCmd->end();
        buildCmd->execute(fence);

        if (!fence->wait())
        {
            throw std::runtime_error("TLAS build wait failed");
        }
    }

    void Scene::loadCamera(const aiScene* pScene, const Options& options)
    {
        if (pScene->HasCameras())
        {
            mCameraExists         = true;
            const auto* pAiCamera = pScene->mCameras[0];

            mCamera.setAspect(1. * *options.width / *options.height);
            mCamera.setFOV(pAiCamera->mHorizontalFOV / mCamera.getAspect());
            mCamera.setPos(convert(pAiCamera->mPosition));
            mCamera.setLookAt(convert(pAiCamera->mLookAt));
            mCamera.setUpVector(convert(pAiCamera->mUp) * glm::vec3(0., -1, 0.));
            mCamera.setNear(pAiCamera->mClipPlaneNear);
            mCamera.setFar(pAiCamera->mClipPlaneFar);

            // FIXME: camera animation
            // const auto* pCameraNode = pScene->mRootNode->FindNode(pAiCamera->mName);
            // const bool res = loadAnimatedTransform(pScene, pCameraNode, mCameraAnimTransform);
        }
    }

    bool Scene::loadAnimatedTransform(const aiScene* pScene, const aiNode* pNode, AnimatedTransform& animTransform_out)
    {
        constexpr double kMicrosecondsPerSecond = 1e6;
        constexpr double kDefaultTicksPerSecond = 1e3;

        const aiAnimation* pAnim   = nullptr;
        const aiNodeAnim* pChannel = nullptr;

        const auto& aiTransform = pNode->mTransformation;

        aiVector3D scale;
        aiQuaternion rotation;
        aiVector3D position;
        aiTransform.Decompose(scale, rotation, position);

        Transform staticTransform{
            .pos   = convert(position),
            .rot   = convert(rotation),
            .scale = convert(scale),
        };

        staticTransform.params.update(staticTransform.pos, staticTransform.rot, staticTransform.scale);

        const auto lmdStoreStaticTransform = [&]() { animTransform_out.animations.emplace(0, staticTransform); };

        if (!pScene->HasAnimations())
        {
            lmdStoreStaticTransform();
            return false;
        }

        std::string_view name(pNode->mName.C_Str());
        for (size_t i = 0; i < pScene->mNumAnimations; ++i)
        {
            const auto* pCandidateAnim = pScene->mAnimations[i];
            for (size_t j = 0; j < pCandidateAnim->mNumChannels; ++j)
            {
                const auto* pCandidateChannel = pCandidateAnim->mChannels[j];
                if (name == pCandidateChannel->mNodeName.C_Str())
                {
                    pAnim    = pCandidateAnim;
                    pChannel = pCandidateChannel;
                    break;
                }
            }

            if (pChannel)
            {
                break;
            }
        }

        if (!pChannel)
        {
            lmdStoreStaticTransform();
            return false;
        }

        const double ticksPerSecond = pAnim->mTicksPerSecond > 0.0 ? pAnim->mTicksPerSecond : kDefaultTicksPerSecond;
        const double ticksToMicroseconds = kMicrosecondsPerSecond / ticksPerSecond;

        std::set<double> keyTimes;
        for (size_t k = 0; k < pChannel->mNumPositionKeys; ++k)
        {
            keyTimes.insert(pChannel->mPositionKeys[k].mTime);
        }

        for (size_t k = 0; k < pChannel->mNumRotationKeys; ++k)
        {
            keyTimes.insert(pChannel->mRotationKeys[k].mTime);
        }

        for (size_t k = 0; k < pChannel->mNumScalingKeys; ++k)
        {
            keyTimes.insert(pChannel->mScalingKeys[k].mTime);
        }

        if (keyTimes.empty())
        {
            lmdStoreStaticTransform();
            return false;
        }

        const auto lmdSampleVector =
            [](const aiVectorKey* keys, size_t keyCount, double time, const glm::vec3& fallback)
        {
            if (keyCount == 0)
            {
                return fallback;
            }

            if (time <= keys[0].mTime)
            {
                return convert(keys[0].mValue);
            }

            if (time >= keys[keyCount - 1].mTime)
            {
                return convert(keys[keyCount - 1].mValue);
            }

            const auto* upper = std::upper_bound(
                keys, keys + keyCount, time, [](double value, const aiVectorKey& key) { return value < key.mTime; });

            const auto* lower = std::prev(upper);

            const float rate = static_cast<float>((time - lower->mTime) / (upper->mTime - lower->mTime));

            return glm::mix(convert(lower->mValue), convert(upper->mValue), rate);
        };

        const auto lmdSampleRotation =
            [](const aiQuatKey* keys, size_t keyCount, double time, const glm::quat& fallback)
        {
            if (keyCount == 0)
            {
                return fallback;
            }

            if (time <= keys[0].mTime)
            {
                return glm::normalize(convert(keys[0].mValue));
            }

            if (time >= keys[keyCount - 1].mTime)
            {
                return glm::normalize(convert(keys[keyCount - 1].mValue));
            }

            const auto* upper = std::upper_bound(keys, keys + keyCount, time,
                                                 [](double value, const aiQuatKey& key) { return value < key.mTime; });

            const auto* lower = std::prev(upper);

            const float rate = static_cast<float>((time - lower->mTime) / (upper->mTime - lower->mTime));

            const auto q0 = glm::normalize(convert(lower->mValue));
            const auto q1 = glm::normalize(convert(upper->mValue));

            return glm::normalize(glm::shortMix(q0, q1, rate));
        };

        for (const double keyTime : keyTimes)
        {
            Transform transform{
                .pos =
                    lmdSampleVector(pChannel->mPositionKeys, pChannel->mNumPositionKeys, keyTime, staticTransform.pos),
                .rot = lmdSampleRotation(pChannel->mRotationKeys, pChannel->mNumRotationKeys, keyTime,
                                         staticTransform.rot),
                .scale =
                    lmdSampleVector(pChannel->mScalingKeys, pChannel->mNumScalingKeys, keyTime, staticTransform.scale),
            };

            transform.params.update(transform.pos, transform.rot, transform.scale);

            const auto time = static_cast<uint32_t>(std::llround(keyTime * ticksToMicroseconds));

            animTransform_out.animations.insert_or_assign(time, transform);
        }

        return true;
    }

}  // namespace evr
