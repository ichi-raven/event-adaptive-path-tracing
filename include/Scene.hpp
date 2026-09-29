/*****************************************************************/ /**
 * @file   Scene.hpp
 * @brief  header file of scene class 
 * 
 * @author ichi-raven
 * @date   June 2024
 *********************************************************************/
#ifndef EVENTRENDERER_SCENE_HPP_
#define EVENTRENDERER_SCENE_HPP_

#include "Mesh.hpp"
#include "Material.hpp"
#include "Emitter.hpp"
#include "Transform.hpp"
#include "AnimatedTransform.hpp"
#include "Options.hpp"

#include <vk2s/Camera.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stb_image.h>

#include <glm/gtc/type_ptr.hpp>

#include <concepts>

#include <vector>
#include <memory>
#include <tuple>
#include <map>
#include <optional>
#include <variant>

#include <cstdint>

namespace evr
{
    using TextureKey = std::tuple<std::string, vk::Format>;

    /**
     * @brief  class representing a scene file
     */
    class Scene
    {
    public:
        /**
         * @brief  constructor (loads a file at the specified path)
         */
        Scene(vk2s::Device& device, const Options& options);

        Scene(const Scene&)            = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&)                 = delete;
        Scene& operator=(Scene&&)      = delete;

        //! @brief destructor
        ~Scene();

        void update(const double t);

        /**
         * @brief  get all meshes in the scene
         */
        const std::vector<Mesh>& getMeshes() const;

        /**
         * @brief  get all materials in the scene
         */
        const std::vector<Material>& getMaterials() const;

        /**
         * @brief  get all textures in the scene
         */
        const std::vector<Handle<vk2s::Image>>& getTextures() const;

        //! @brief get all emitters in the scene
        //! @return emitters
        const std::vector<Emitter>& getEmitters() const;

        //! @brief get camera reference
        //! @return camera reference
        const vk2s::Camera& getCamera() const;

        vk2s::Camera& getCameraRef();

        //! @brief get TLAS for the specified frame index
        //! @param frameIndex
        //! @return TLAS handle
        const Handle<vk2s::AccelerationStructure>& getTLAS(const uint32_t frameIndex) const;

        const Handle<vk2s::AccelerationStructure>& getTLAS(const double t0, const double t1) const;

        //! @brief get AnimatedTransforms of all objects
        //! @return AnimatedTransforms
        const std::vector<AnimatedTransform>& getAnimatedTransforms() const;

    private:
        /**
         * @brief  per-node processing (recursive)
         */
        void processNode(const aiScene* pScene, const aiNode* node,
                         std::shared_ptr<AnimatedTransform> parent = nullptr);

        /**
         * @brief  loading of the mesh attached to the node
         */
        bool processMesh(const aiScene* pScene, const aiMesh* pMesh, const AnimatedTransform& animTransform,
                         bool hasAnimation);

        //! @brief build TLAS for the specified time pair (frame)
        //! @param t
        //! @param command
        //! @return
        Handle<vk2s::AccelerationStructure> buildTLAS(const std::pair<double, double> t, Handle<vk2s::Command> command);

        /**
         * @brief loading of materials corresponding to the mesh 
         */
        std::pair<uint32_t, bool> addMaterial(const aiScene* pScene, const aiMesh* pMesh);

        /**
         * @brief  load the textures that come with the material
         */
        uint32_t loadMaterialTexture(const aiScene* pScene, const aiMaterial* mat, const aiTextureType aiType,
                                     vk::Format format);

        Handle<vk2s::Image> loadTextureImage(const aiScene* pScene, const aiString& texturePath, vk::Format format);

        void loadCamera(const aiScene* pScene, const Options& options);

        bool loadAnimatedTransform(const aiScene* pScene, const aiNode* pNode, AnimatedTransform& animTransform_out);

        //! @brief build BLAS, TLAS and transition texture images together
        void build();

    private:
        //! directory of read files
        std::string mDirectory;
        //! path of read files
        std::string mPath;

        //! meshes of the scene
        std::vector<Mesh> mMeshes;
        //! materials of the scene
        std::vector<Material> mMaterials;
        //! textures of the scene
        std::vector<Handle<vk2s::Image>> mTextures;
        //! emitters of the scene
        std::vector<Emitter> mEmitters;
        //! animated transforms of the scene
        std::vector<AnimatedTransform> mAnimTransforms;

        //! TLAS for each frame
        std::vector<Handle<vk2s::AccelerationStructure>> mTLASs;

        //! mapping path to index
        std::map<TextureKey, uint32_t> mTextureMap;

        //! ref to device
        vk2s::Device& mDevice;

        //! ref to options
        Options mOptions;

        //! camera
        vk2s::Camera mCamera;
        bool mCameraExists;
        AnimatedTransform mCameraAnimTransform;

        //! Assimp importer
        Assimp::Importer mImporter;
    };
}  // namespace evr

#endif
