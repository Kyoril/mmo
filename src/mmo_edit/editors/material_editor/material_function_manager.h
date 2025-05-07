#pragma once

#include "base/non_copyable.h"
#include "material_function.h"

#include <map>
#include <memory>
#include <string_view>

#include "base/utilities.h"

namespace mmo
{
    /// @brief Class which manages all material functions.
    class MaterialFunctionManager final : public NonCopyable
    {
        friend std::unique_ptr<MaterialFunctionManager> std::make_unique<MaterialFunctionManager>();

    private:
        MaterialFunctionManager() = default;

    public:
        ~MaterialFunctionManager() override = default;

    public:
        static MaterialFunctionManager& Get();
        
    public:
        /// Loads a material function from file or retrieves it from the cache.
        MaterialFunctionPtr Load(std::string_view filename);

        /// Creates a material function manually.
        MaterialFunctionPtr CreateManual(const std::string_view name);

        /// Removes a material function.
        void Remove(std::string_view filename);

        /// Removes all unreferenced material functions from memory.
        void RemoveAllUnreferenced();

    private:
        std::map<std::string, MaterialFunctionPtr, StrCaseIComp> m_materialFunctions;
    };
}