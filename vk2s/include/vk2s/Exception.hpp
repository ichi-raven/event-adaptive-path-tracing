/**********************************************************************
 * @file   Exception.hpp
 * @brief  template definition of class Exception
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/

#include <string>
#include <stdexcept>

namespace vk2s
{
    class VkException : public std::exception
    {
    public:
        explicit VkException(const std::string& msg)
            : mMsg(msg)
        {
        }

        virtual const char* what() const noexcept override
        {
            return mMsg.c_str();
        }

    private:
        std::string mMsg;
    };
}  // namespace vk2s
