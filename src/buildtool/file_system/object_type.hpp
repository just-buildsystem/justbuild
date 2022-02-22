#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_TYPE_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_TYPE_HPP

enum class ObjectType {
    File,
    Executable,
    Tree,
};

[[nodiscard]] constexpr auto FromChar(char c) -> ObjectType {
    switch (c) {
        case 'x':
            return ObjectType::Executable;
        case 't':
            return ObjectType::Tree;
        default:
            return ObjectType::File;
    }
}

[[nodiscard]] constexpr auto ToChar(ObjectType type) -> char {
    switch (type) {
        case ObjectType::File:
            return 'f';
        case ObjectType::Executable:
            return 'x';
        case ObjectType::Tree:
            return 't';
    }
}

[[nodiscard]] constexpr auto IsFileObject(ObjectType type) -> bool {
    return type == ObjectType::Executable or type == ObjectType::File;
}

[[nodiscard]] constexpr auto IsExecutableObject(ObjectType type) -> bool {
    return type == ObjectType::Executable;
}

[[nodiscard]] constexpr auto IsTreeObject(ObjectType type) -> bool {
    return type == ObjectType::Tree;
}

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_TYPE_HPP
