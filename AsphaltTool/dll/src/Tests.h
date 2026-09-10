#pragma once
#include <string>
#include <filesystem>

//fwd
namespace BulletTypes
{
    class CollisionObject;
}

namespace AsphaltDLL
{
    namespace Tests
    {
        void LoadCustomTrack(const std::filesystem::path& path) noexcept;
        void BuildFlatGroundScene() noexcept;

        void PrintCollisionObjectTest(BulletTypes::CollisionObject* obj) noexcept;

        void ChangeMaterialsTest() noexcept;
        
        void DebugDumpPhysicsWorldObjects(const std::string& path) noexcept;
        
        void MovePhysicsObjectsTest() noexcept;

        void RaycastTest() noexcept;
    }
}
