/*****************************************************************/ /**
 * @file   Macro.hpp
 * @brief  Macros for convenient class declaration and implementation
 * 
 * @author ichi-raven
 * @date   November 2024
 *********************************************************************/

//! constructor generation macros for non-copyable class declaration
#define NONCOPYABLE(TypeName)                      \
    TypeName(const TypeName& other)      = delete; \
    TypeName& operator=(TypeName& other) = delete;

//! constructor generation macros for non-movable class declaration
#define NONMOVABLE(TypeName)                         \
    TypeName(const TypeName&& other)       = delete; \
    TypeName&& operator=(TypeName&& other) = delete;
