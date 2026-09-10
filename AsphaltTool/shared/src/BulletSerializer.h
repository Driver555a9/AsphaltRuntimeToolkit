#pragma once

#include "BulletTypes.h"

#include "nlohmann/json.hpp"
#include "nlohmann/json_fwd.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
#include <iostream>

namespace BulletTypes
{
    namespace Serializer
    {
        enum class CollisionSerializerVersion : uint32_t
        {
            VERSION_1 = 1,
            VERSION_2 = 2,
            NEWEST    = VERSION_2
        };

        constexpr uint32_t MAGIC_HEADER = 0x4C4F4342; // BCOL
        static_assert(MAGIC_HEADER == 0x4C4F4342);

        namespace Keys
        {
            namespace Meta
            {
                constexpr char ROOT[]      = "Meta";
                constexpr char VERSION[]   = "Version";
                constexpr char TIMESTAMP[] = "Timestamp";
            }

            namespace ShapesRoot
            {
                constexpr char ROOT[]              = "Shapes";
                constexpr char SHAPE_TYPE[]        = "Type";
                constexpr char MARGIN[]            = "Margin";
                constexpr char HALF_EXTENTS[]      = "HalfExtents";
                constexpr char UP_AXYS[]           = "UpAxys";
                constexpr char LOCAL_SCALE[]       = "LocalScale";
                constexpr char LOCAL_TRANSFORM[]   = "LocalTrans";
                constexpr char BYTE_OFFSET[]       = "ByteOffset";
                constexpr char BYTE_LENGTH[]       = "ByteLength";
                constexpr char CHILD_SHAPE_INDEX[] = "ChildIndex";
                constexpr char CHILDREN[]          = "Children";
            }

            namespace ObjectsRoot
            {
                constexpr char ROOT[]                    = "Objects"; 
                constexpr char TRANSFORM[]               = "Trans";
                constexpr char COLLISION_FLAGS[]         = "ColFlags";
                constexpr char INTERNAL_TYPE[]           = "InternalType";
                constexpr char MASS[]                    = "Mass";
                constexpr char SHAPE_INDEX[]             = "ShapeIndex";
            }
        };

        struct BinaryTriangle 
        { 
            constexpr static int32_t INVALID_MATERIAL_INDEX = 0xFFFFFFFF;
            UnalignedVector3 m_vert_a, m_vert_b, m_vert_c; 
            int32_t m_index_material {INVALID_MATERIAL_INDEX}; 
        }; 

        class TriangleExtract : public BulletTypes::TriangleCallback
        {
        private:
            struct SubpartMaterialCache
            {
                const unsigned char* material_base = nullptr;
                int num_materials = 0;
                PHY_ScalarType material_type;
                int material_stride = 0;
                const unsigned char* triangle_material_base = nullptr;
                int num_triangles = 0;
                int triangle_material_stride = 0;
                PHY_ScalarType triangle_type;
            };

            std::vector<BinaryTriangle> m_triangles;
            std::vector<SubpartMaterialCache> m_subpart_caches;
            const BulletTypes::TriangleIndexVertexMaterialArray* m_material_array = nullptr;

        public:
            TriangleExtract(const BulletTypes::TriangleIndexVertexMaterialArray* material_array = nullptr) noexcept : m_material_array(material_array) 
            {
                if (m_material_array)
                {
                    int num_subparts = m_material_array->m_indexed_meshes.m_size;
                    m_subpart_caches.resize(num_subparts);

                    for (int i = 0; i < num_subparts; ++i)
                    {
                        m_material_array->GetLockedReadOnlyMaterialBase(
                            &m_subpart_caches[i].material_base,
                            m_subpart_caches[i].num_materials,
                            m_subpart_caches[i].material_type,
                            m_subpart_caches[i].material_stride,
                            &m_subpart_caches[i].triangle_material_base,
                            m_subpart_caches[i].num_triangles,
                            m_subpart_caches[i].triangle_material_stride,
                            m_subpart_caches[i].triangle_type,
                            i
                        );
                    }
                }
            }

            virtual ~TriangleExtract() noexcept override = default;

            virtual void ProcessTriangle(const Vector3* triangle_vertices, int subpart_index, int triangle_index) noexcept override
            {
                decltype(BinaryTriangle::m_index_material) material_index = BinaryTriangle::INVALID_MATERIAL_INDEX;

                if (m_material_array && subpart_index >= 0 && subpart_index < static_cast<int>(m_subpart_caches.size()))
                {
                    const auto& cache = m_subpart_caches[subpart_index];

                    if (cache.triangle_material_base && triangle_index >= 0 && triangle_index < cache.num_triangles)
                    {
                        const unsigned char* tri_mat_ptr = cache.triangle_material_base + (triangle_index * cache.triangle_material_stride);

                        material_index = static_cast<decltype(material_index)>(*tri_mat_ptr);
                    }
                }

                m_triangles.push_back(BinaryTriangle
                {
                    UnalignedVector3{ triangle_vertices[0].x, triangle_vertices[0].y, triangle_vertices[0].z },
                    UnalignedVector3{ triangle_vertices[1].x, triangle_vertices[1].y, triangle_vertices[1].z },
                    UnalignedVector3{ triangle_vertices[2].x, triangle_vertices[2].y, triangle_vertices[2].z },
                    material_index,
                });
            }

            void Reserve(size_t amount) noexcept { m_triangles.reserve(amount); }
            const std::vector<BinaryTriangle>& GetTriangles() const noexcept { return m_triangles; }
        };

        struct CollisionObjectInfo
        {
            UnalignedTransform m_world_transform;
            uint32_t           m_collision_flags;
            int                m_internal_type;
            float              m_mass;
        };

        struct BasicExtractedShape
        {
            BroadphaseNativeTypes m_shape_type;
            float m_margin;
            virtual ~BasicExtractedShape() = default;
        protected:
            BasicExtractedShape(BroadphaseNativeTypes t) : m_shape_type(t) {}
        };

        struct ExtractedObject
        {
            CollisionObjectInfo m_collision_object_info;
            std::unique_ptr<BasicExtractedShape> m_root_shape;
        };

        struct BoxExtractedShape : public BasicExtractedShape 
        {
            UnalignedVector3 m_implicit_shape_dimensions;
            BoxExtractedShape() : BasicExtractedShape(BroadphaseNativeTypes::BOX_SHAPE_PROXYTYPE) {}
        };

        struct SphereExtractedShape : public BasicExtractedShape
        {
            UnalignedVector3 m_implicit_shape_dimensions;
            SphereExtractedShape() : BasicExtractedShape(BroadphaseNativeTypes::SPHERE_SHAPE_PROXYTYPE) {}
        };

        struct CapsuleExtractedShape : public BasicExtractedShape
        {   
            UnalignedVector3 m_implicit_shape_dimensions;
            int m_up_axis;
            CapsuleExtractedShape() : BasicExtractedShape(BroadphaseNativeTypes::CAPSULE_SHAPE_PROXYTYPE) {}
        };

        struct CylinderExtractedShape : public BasicExtractedShape
        {
            UnalignedVector3 m_implicit_shape_dimensions;
            int m_up_axis;
            CylinderExtractedShape() : BasicExtractedShape(BroadphaseNativeTypes::CYLINDER_SHAPE_PROXYTYPE) {}
        };

        struct MultiMatExtractedShape : public BasicExtractedShape
        {
            std::vector<BinaryTriangle> m_triangles;
            MultiMatExtractedShape() : BasicExtractedShape(BroadphaseNativeTypes::MULTIMATERIAL_TRIANGLE_MESH_PROXYTYPE) {}
        };

        struct ScaledTriangleMeshExtractedShape : public BasicExtractedShape
        {
            UnalignedVector3 m_scale;
            std::unique_ptr<MultiMatExtractedShape> m_internal_triangle_shape; // Update this if we find other internal types
            ScaledTriangleMeshExtractedShape()  : BasicExtractedShape(BroadphaseNativeTypes::SCALED_TRIANGLE_MESH_SHAPE_PROXYTYPE) {}
        };

        struct CompoundExtractedShape : public BasicExtractedShape
        {
            struct Child 
            {
                UnalignedTransform m_local_transform;
                std::unique_ptr<BasicExtractedShape> m_child_ptr;
            };

            std::vector<Child> m_children;
            CompoundExtractedShape() : BasicExtractedShape(BroadphaseNativeTypes::COMPOUND_SHAPE_PROXYTYPE) {}
        };

        template <typename TStream>
        bool SerializeObjectsToStream(TStream& stream, const std::vector<CollisionObject*>& objects)
        {
            nlohmann::ordered_json json = nlohmann::ordered_json::object();
            json[Keys::Meta::ROOT][Keys::Meta::TIMESTAMP] = std::time(nullptr);
            json[Keys::Meta::ROOT][Keys::Meta::VERSION]   = static_cast<uint32_t>(CollisionSerializerVersion::NEWEST);

            json[Keys::ObjectsRoot::ROOT] = nlohmann::ordered_json::array();

            std::vector<uint8_t> binary_payload;
            binary_payload.reserve(1024 * 1024);

            std::unordered_map<const BulletTypes::CollisionShape*, size_t> serialized_shapes;
            std::vector<nlohmann::ordered_json> shape_slots;

            const auto SerializeBox = [](nlohmann::ordered_json& sh_json, const BoxShape* box)
            {
                sh_json[Keys::ShapesRoot::SHAPE_TYPE]   = box->m_shape_type;
                sh_json[Keys::ShapesRoot::MARGIN]       = box->GetMargin();
                const Vector3 extents = box->m_implicit_shape_dimensions;
                sh_json[Keys::ShapesRoot::HALF_EXTENTS] = { extents.x, extents.y, extents.z };
                return true;
            };

            const auto SerializeSphere = [](nlohmann::ordered_json& sh_json, const SphereShape* sphere)
            {
                sh_json[Keys::ShapesRoot::SHAPE_TYPE]   = sphere->m_shape_type;
                sh_json[Keys::ShapesRoot::MARGIN]       = sphere->GetMargin();
                const Vector3 extents = sphere->m_implicit_shape_dimensions;
                sh_json[Keys::ShapesRoot::HALF_EXTENTS] = { extents.x, extents.y, extents.z };
                return true;
            };

            const auto SerializeCapsule = [](nlohmann::ordered_json& sh_json, const CapsuleShape* capsule)
            {
                sh_json[Keys::ShapesRoot::SHAPE_TYPE]   = capsule->m_shape_type;
                sh_json[Keys::ShapesRoot::MARGIN]       = capsule->GetMargin();
                const Vector3 extents = capsule->m_implicit_shape_dimensions;
                sh_json[Keys::ShapesRoot::HALF_EXTENTS] = { extents.x, extents.y, extents.z };
                sh_json[Keys::ShapesRoot::UP_AXYS]      = capsule->m_up_axis;
                return true;
            };

            const auto SerializeCylinder = [](nlohmann::ordered_json& sh_json, const CylinderShape* cylinder)
            {
                sh_json[Keys::ShapesRoot::SHAPE_TYPE]   = cylinder->m_shape_type;
                sh_json[Keys::ShapesRoot::MARGIN]       = cylinder->GetMargin();
                const Vector3 extents                   = cylinder->m_implicit_shape_dimensions;
                sh_json[Keys::ShapesRoot::HALF_EXTENTS] = { extents.x, extents.y, extents.z };
                sh_json[Keys::ShapesRoot::UP_AXYS]      = cylinder->m_up_axis;
                return true;
            };

            const auto SerializeMultiMat = [](nlohmann::ordered_json& sh_json, std::vector<uint8_t>& bin_payload, const MultimaterialTriangleMeshShape* multimat)
            {
                sh_json[Keys::ShapesRoot::SHAPE_TYPE] = multimat->m_shape_type;
                sh_json[Keys::ShapesRoot::MARGIN]     = multimat->GetMargin();

                const size_t byte_offset = bin_payload.size();

                const TriangleIndexVertexMaterialArray* arr = nullptr;
                if (multimat->m_mesh_interface && multimat->m_mesh_interface->IsInternalTriangleVertexMaterialArray())
                {
                    arr = reinterpret_cast<const TriangleIndexVertexMaterialArray*>(multimat->m_mesh_interface);
                }

                TriangleExtract extractor(arr);
                extractor.Reserve(multimat->GetAmountTriangles() * 3);
                multimat->ProcessAllTriangles(&extractor, {-10E9f, -10E9f, -10E9f}, {10E9f, 10E9f, 10E9f});

                const std::vector<BinaryTriangle>& triangles = extractor.GetTriangles();
                const uint8_t* bytes     = reinterpret_cast<const uint8_t*>(triangles.data());
                const size_t byte_length = triangles.size() * sizeof(BinaryTriangle);
                bin_payload.insert(bin_payload.end(), bytes, bytes + byte_length);

                sh_json[Keys::ShapesRoot::BYTE_OFFSET] = byte_offset;
                sh_json[Keys::ShapesRoot::BYTE_LENGTH] = byte_length;
                return true;
            };

            std::function<size_t(const BulletTypes::CollisionShape*)> GetOrSerializeShape;

            GetOrSerializeShape = [&](const BulletTypes::CollisionShape* shape) -> size_t
            {
                if (!shape) return static_cast<size_t>(-1);

                if (auto it = serialized_shapes.find(shape); it != serialized_shapes.end())
                {
                    return it->second;
                }

                const size_t current_shape_index = shape_slots.size();
                serialized_shapes[shape] = current_shape_index;
                shape_slots.emplace_back();

                nlohmann::ordered_json shape_json;
                bool serialize_success = false;

                if (const BoxShape* box = shape->As<const BoxShape*>())
                {
                    serialize_success = SerializeBox(shape_json, box);
                }
                else if (const SphereShape* sphere = shape->As<const SphereShape*>())
                {
                    serialize_success = SerializeSphere(shape_json, sphere);
                }
                else if (const CapsuleShape* capsule = shape->As<const CapsuleShape*>())
                {
                    serialize_success = SerializeCapsule(shape_json, capsule);
                }
                else if (const CylinderShape* cylinder = shape->As<const CylinderShape*>())
                {
                    serialize_success = SerializeCylinder(shape_json, cylinder);
                }
                else if (const MultimaterialTriangleMeshShape* multimat = shape->As<const MultimaterialTriangleMeshShape*>())
                {
                    serialize_success = SerializeMultiMat(shape_json, binary_payload, multimat);
                }
                else if (const ScaledBvhTriangleMeshShape* scaledtri = shape->As<const ScaledBvhTriangleMeshShape*>())
                {
                    shape_json[Keys::ShapesRoot::SHAPE_TYPE] = scaledtri->m_shape_type;
                    shape_json[Keys::ShapesRoot::MARGIN]     = scaledtri->GetMargin();
                    const Vector3& scale = scaledtri->m_local_scaling;
                    shape_json[Keys::ShapesRoot::LOCAL_SCALE] = { scale.x, scale.y, scale.z };

                    if (scaledtri->m_bvh_tri_mesh_shape)
                    {
                        const size_t child_index = GetOrSerializeShape(scaledtri->m_bvh_tri_mesh_shape);
                        if (child_index != static_cast<size_t>(-1))
                        {
                            shape_json[Keys::ShapesRoot::CHILD_SHAPE_INDEX] = child_index;
                            serialize_success = true;
                        }
                    }
                }
                else if (const CompoundShape* compound = shape->As<const CompoundShape*>())
                {
                    shape_json[Keys::ShapesRoot::SHAPE_TYPE] = compound->m_shape_type;
                    shape_json[Keys::ShapesRoot::MARGIN]     = compound->GetMargin();

                    nlohmann::ordered_json children_array = nlohmann::ordered_json::array();
                    const auto& child_list = compound->m_children;

                    for (int i = 0; i < child_list.m_size; ++i)
                    {
                        const auto& child = child_list[i];
                        if (!child.m_child_shape) continue;

                        const size_t child_index = GetOrSerializeShape(child.m_child_shape);
                        if (child_index == static_cast<size_t>(-1)) continue;

                        nlohmann::ordered_json child_json;
                        const Transform& child_trans = child.m_transform;
                        child_json[Keys::ShapesRoot::LOCAL_TRANSFORM] =
                        {
                            child_trans[0].x, child_trans[0].y, child_trans[0].z, child_trans[0].w,
                            child_trans[1].x, child_trans[1].y, child_trans[1].z, child_trans[1].w,
                            child_trans[2].x, child_trans[2].y, child_trans[2].z, child_trans[2].w,
                            child_trans[3].x, child_trans[3].y, child_trans[3].z, child_trans[3].w
                        };
                        child_json[Keys::ShapesRoot::CHILD_SHAPE_INDEX] = child_index;
                        children_array.push_back(std::move(child_json));
                    }

                    shape_json[Keys::ShapesRoot::CHILDREN] = std::move(children_array);
                    serialize_success = true;
                }
                else
                {
                    std::cerr << "ERROR: Unknown Shape Type skipped: " << shape->m_shape_type << "\n";
                }

                if (serialize_success)
                {
                    shape_slots[current_shape_index] = std::move(shape_json);
                }

                return current_shape_index;
            };

            for (size_t i = 0; i < objects.size(); ++i)
            {
                const auto& obj = objects[i];
                const auto* shape = obj->m_collision_shape_ptr;
                if (!shape) continue;

                size_t shape_index = GetOrSerializeShape(shape);
                if (shape_index == static_cast<size_t>(-1)) continue;

                nlohmann::ordered_json obj_json;

                obj_json[Keys::ObjectsRoot::SHAPE_INDEX]             = shape_index;
                obj_json[Keys::ObjectsRoot::COLLISION_FLAGS]         = obj->m_collision_flags;
                obj_json[Keys::ObjectsRoot::INTERNAL_TYPE]           = obj->m_internal_type;

                if (obj->IsRigidBody())
                {
                    obj_json[Keys::ObjectsRoot::MASS] = (1.0f / static_cast<const RigidBody*>(obj)->m_inverse_mass);
                }
                else 
                {
                    obj_json[Keys::ObjectsRoot::MASS] = 0.0f;
                }

                const Transform& trans = obj->m_transform_matrix;
                obj_json[Keys::ObjectsRoot::TRANSFORM] =
                {
                    trans[0].x, trans[0].y, trans[0].z, trans[0].w,
                    trans[1].x, trans[1].y, trans[1].z, trans[1].w,
                    trans[2].x, trans[2].y, trans[2].z, trans[2].w,
                    trans[3].x, trans[3].y, trans[3].z, trans[3].w
                };

                json[Keys::ObjectsRoot::ROOT].push_back(std::move(obj_json));
            }

            json[Keys::ShapesRoot::ROOT] = nlohmann::ordered_json::array();
            for (auto& slot : shape_slots)
            {
                json[Keys::ShapesRoot::ROOT].push_back(std::move(slot));
            }

            const std::string manifest_str = json.dump(2);
            const uint32_t manifest_size = static_cast<uint32_t>(manifest_str.size());
            stream.write(reinterpret_cast<const char*>(&MAGIC_HEADER), sizeof(MAGIC_HEADER));
            stream.write(reinterpret_cast<const char*>(&manifest_size), sizeof(manifest_size));
            stream.write(manifest_str.data(), manifest_size);

            const uint32_t payload_size = static_cast<uint32_t>(binary_payload.size());
            stream.write(reinterpret_cast<const char*>(&payload_size), sizeof(payload_size));
            if (payload_size > 0)
            {
                stream.write(reinterpret_cast<const char*>(binary_payload.data()), payload_size);
            }

            return true;
        }

        inline std::unique_ptr<BasicExtractedShape> CloneExtractedShape(const BasicExtractedShape* src)
        {
            if (!src) return nullptr;
            
            switch (src->m_shape_type)
            {
                case BroadphaseNativeTypes::BOX_SHAPE_PROXYTYPE:
                {
                    auto dst = std::make_unique<BoxExtractedShape>();
                    *dst = *static_cast<const BoxExtractedShape*>(src);
                    return dst;
                }
                case BroadphaseNativeTypes::SPHERE_SHAPE_PROXYTYPE:
                {
                    auto dst = std::make_unique<SphereExtractedShape>();
                    *dst = *static_cast<const SphereExtractedShape*>(src);
                    return dst;
                }
                case BroadphaseNativeTypes::CAPSULE_SHAPE_PROXYTYPE:
                {
                    auto dst = std::make_unique<CapsuleExtractedShape>();
                    *dst = *static_cast<const CapsuleExtractedShape*>(src);
                    return dst;
                }
                case BroadphaseNativeTypes::CYLINDER_SHAPE_PROXYTYPE:
                {
                    auto dst = std::make_unique<CylinderExtractedShape>();
                    *dst = *static_cast<const CylinderExtractedShape*>(src);
                    return dst;
                }
                case BroadphaseNativeTypes::MULTIMATERIAL_TRIANGLE_MESH_PROXYTYPE:
                {
                    auto dst = std::make_unique<MultiMatExtractedShape>();
                    *dst = *static_cast<const MultiMatExtractedShape*>(src);
                    return dst;
                }
                case BroadphaseNativeTypes::SCALED_TRIANGLE_MESH_SHAPE_PROXYTYPE:
                {
                    auto dst = std::make_unique<ScaledTriangleMeshExtractedShape>();
                    const auto* scaled_src = static_cast<const ScaledTriangleMeshExtractedShape*>(src);
                    dst->m_shape_type = scaled_src->m_shape_type;
                    dst->m_margin     = scaled_src->m_margin;
                    dst->m_scale      = scaled_src->m_scale;

                    if (scaled_src->m_internal_triangle_shape)
                    {
                        auto cloned_internal = CloneExtractedShape(scaled_src->m_internal_triangle_shape.get());
                        dst->m_internal_triangle_shape.reset(static_cast<MultiMatExtractedShape*>(cloned_internal.release()));
                    }
                    return dst;
                }
                case BroadphaseNativeTypes::COMPOUND_SHAPE_PROXYTYPE:
                {
                    auto dst = std::make_unique<CompoundExtractedShape>();
                    const auto* comp_src = static_cast<const CompoundExtractedShape*>(src);
                    dst->m_shape_type = comp_src->m_shape_type;
                    dst->m_margin     = comp_src->m_margin;

                    for (const auto& child : comp_src->m_children)
                    {
                        CompoundExtractedShape::Child cloned_child;
                        cloned_child.m_local_transform = child.m_local_transform;
                        cloned_child.m_child_ptr       = CloneExtractedShape(child.m_child_ptr.get());
                        dst->m_children.push_back(std::move(cloned_child));
                    }
                    return dst;
                }
                default:
                    return nullptr;
            }
        }

        [[nodiscard]] inline std::vector<ExtractedObject> DeserializeObjectsFromDataBuffer(const char* buffer, size_t buffer_size)
        {
            constexpr size_t HEADER_OVERHEAD = sizeof(uint32_t) + sizeof(uint32_t);
            if (!buffer || buffer_size < HEADER_OVERHEAD) return {};

            size_t read_offset = 0;

            uint32_t magic = 0;
            std::memcpy(&magic, buffer + read_offset, sizeof(uint32_t));
            read_offset += sizeof(uint32_t);

            if (magic != MAGIC_HEADER)
            {
                std::cerr << "ERROR: Invalid binary header magic.\n";
                return {};
            }

            uint32_t manifest_size = 0;
            std::memcpy(&manifest_size, buffer + read_offset, sizeof(uint32_t));
            read_offset += sizeof(uint32_t);

            if (buffer_size < read_offset + manifest_size + sizeof(uint32_t))
            {
                std::cerr << "ERROR: Buffer truncated before manifest / payload size read.\n";
                return {};
            }

            nlohmann::ordered_json manifest_json;
            try
            {
                manifest_json = nlohmann::ordered_json::parse(buffer + read_offset, buffer + read_offset + manifest_size);
            }
            catch (const std::exception& e)
            {
                std::cerr << "ERROR: JSON parse failed: " << e.what() << "\n";
                return {};
            }
            read_offset += manifest_size;

            uint32_t payload_size = 0;
            std::memcpy(&payload_size, buffer + read_offset, sizeof(uint32_t));
            read_offset += sizeof(uint32_t);

            if (buffer_size < read_offset + payload_size)
            {
                std::cerr << "ERROR: Buffer truncated before binary payload end.\n";
                return {};
            }

            const uint8_t* binary_payload_ptr = reinterpret_cast<const uint8_t*>(buffer + read_offset);

            const auto SafeGetNumber = []<typename T>(const nlohmann::json& j, const std::string& key, T default_val) -> T
            {
                if (j.contains(key))
                {
                    const auto& item = j[key];
                    if (item.is_number())
                    {
                        return item.get<T>();
                    }
                }
                return default_val;
            };

            nlohmann::ordered_json empty_array = nlohmann::ordered_json::array();
            const nlohmann::ordered_json& shapes_array = (manifest_json.contains(Keys::ShapesRoot::ROOT) && manifest_json[Keys::ShapesRoot::ROOT].is_array())
                ? manifest_json[Keys::ShapesRoot::ROOT] : empty_array;

            std::vector<std::unique_ptr<BasicExtractedShape>> shape_lookup(shapes_array.size());
            std::vector<uint8_t> being_built(shapes_array.size(), 0);

            std::function<BasicExtractedShape*(size_t)> Materialize;

            Materialize = [&](size_t idx) -> BasicExtractedShape*
            {
                if (idx >= shapes_array.size()) return nullptr;
                if (shape_lookup[idx]) return shape_lookup[idx].get();
                if (being_built[idx]) return nullptr;
                being_built[idx] = 1;

                const auto& sh_json = shapes_array[idx];
                if (!sh_json.is_object())
                {
                    being_built[idx] = 0;
                    return nullptr;
                }

                try
                {
                    auto shape_type = SafeGetNumber(sh_json, Keys::ShapesRoot::SHAPE_TYPE, BroadphaseNativeTypes::INVALID_SHAPE_PROXYTYPE);
                    float margin = SafeGetNumber(sh_json, "Margin", 0.04f);

                    switch (shape_type)
                    {
                        case BroadphaseNativeTypes::BOX_SHAPE_PROXYTYPE:
                        {
                            auto box = std::make_unique<BoxExtractedShape>();
                            box->m_margin = margin;
                            if (sh_json.contains(Keys::ShapesRoot::HALF_EXTENTS) && sh_json[Keys::ShapesRoot::HALF_EXTENTS].is_array() && sh_json[Keys::ShapesRoot::HALF_EXTENTS].size() >= 3)
                            {
                                const auto& ext = sh_json[Keys::ShapesRoot::HALF_EXTENTS];
                                box->m_implicit_shape_dimensions = {
                                    ext[0].is_number() ? ext[0].get<float>() : 0.0f,
                                    ext[1].is_number() ? ext[1].get<float>() : 0.0f,
                                    ext[2].is_number() ? ext[2].get<float>() : 0.0f
                                };
                            }
                            shape_lookup[idx] = std::move(box);
                            break;
                        }
                        case BroadphaseNativeTypes::SPHERE_SHAPE_PROXYTYPE:
                        {
                            auto sphere = std::make_unique<SphereExtractedShape>();
                            sphere->m_margin = margin;
                            if (sh_json.contains(Keys::ShapesRoot::HALF_EXTENTS) && sh_json[Keys::ShapesRoot::HALF_EXTENTS].is_array() && sh_json[Keys::ShapesRoot::HALF_EXTENTS].size() >= 3)
                            {
                                const auto& ext = sh_json[Keys::ShapesRoot::HALF_EXTENTS];
                                sphere->m_implicit_shape_dimensions = {
                                    ext[0].is_number() ? ext[0].get<float>() : 0.0f,
                                    ext[1].is_number() ? ext[1].get<float>() : 0.0f,
                                    ext[2].is_number() ? ext[2].get<float>() : 0.0f
                                };
                            }
                            shape_lookup[idx] = std::move(sphere);
                            break;
                        }
                        case BroadphaseNativeTypes::CAPSULE_SHAPE_PROXYTYPE:
                        {
                            auto capsule = std::make_unique<CapsuleExtractedShape>();
                            capsule->m_margin  = margin;
                            capsule->m_up_axis = SafeGetNumber(sh_json, Keys::ShapesRoot::UP_AXYS, 1);
                            if (sh_json.contains(Keys::ShapesRoot::HALF_EXTENTS) && sh_json[Keys::ShapesRoot::HALF_EXTENTS].is_array() && sh_json[Keys::ShapesRoot::HALF_EXTENTS].size() >= 3)
                            {
                                const auto& ext = sh_json[Keys::ShapesRoot::HALF_EXTENTS];
                                capsule->m_implicit_shape_dimensions = {
                                    ext[0].is_number() ? ext[0].get<float>() : 0.0f,
                                    ext[1].is_number() ? ext[1].get<float>() : 0.0f,
                                    ext[2].is_number() ? ext[2].get<float>() : 0.0f
                                };
                            }
                            shape_lookup[idx] = std::move(capsule);
                            break;
                        }
                        case BroadphaseNativeTypes::CYLINDER_SHAPE_PROXYTYPE:
                        {
                            auto cylinder = std::make_unique<CylinderExtractedShape>();
                            cylinder->m_margin  = margin;
                            cylinder->m_up_axis = SafeGetNumber(sh_json, Keys::ShapesRoot::UP_AXYS, 1);
                            if (sh_json.contains(Keys::ShapesRoot::HALF_EXTENTS) && sh_json[Keys::ShapesRoot::HALF_EXTENTS].is_array() && sh_json[Keys::ShapesRoot::HALF_EXTENTS].size() >= 3)
                            {
                                const auto& ext = sh_json[Keys::ShapesRoot::HALF_EXTENTS];
                                cylinder->m_implicit_shape_dimensions = {
                                    ext[0].is_number() ? ext[0].get<float>() : 0.0f,
                                    ext[1].is_number() ? ext[1].get<float>() : 0.0f,
                                    ext[2].is_number() ? ext[2].get<float>() : 0.0f
                                };
                            }
                            shape_lookup[idx] = std::move(cylinder);
                            break;
                        }
                        case BroadphaseNativeTypes::MULTIMATERIAL_TRIANGLE_MESH_PROXYTYPE:
                        {
                            auto multimat = std::make_unique<MultiMatExtractedShape>();
                            multimat->m_margin = margin;

                            size_t byte_offset = SafeGetNumber(sh_json, Keys::ShapesRoot::BYTE_OFFSET, size_t(0));
                            size_t byte_length = SafeGetNumber(sh_json, Keys::ShapesRoot::BYTE_LENGTH, size_t(0));

                            if (byte_offset + byte_length <= payload_size && byte_length > 0)
                            {
                                size_t triangle_count = byte_length / sizeof(BinaryTriangle);
                                multimat->m_triangles.resize(triangle_count);
                                std::memcpy(multimat->m_triangles.data(), binary_payload_ptr + byte_offset, byte_length);
                            }
                            shape_lookup[idx] = std::move(multimat);
                            break;
                        }
                        case BroadphaseNativeTypes::SCALED_TRIANGLE_MESH_SHAPE_PROXYTYPE:
                        {
                            auto scaled = std::make_unique<ScaledTriangleMeshExtractedShape>();
                            scaled->m_margin = margin;

                            if (sh_json.contains(Keys::ShapesRoot::LOCAL_SCALE) && sh_json[Keys::ShapesRoot::LOCAL_SCALE].is_array() && sh_json[Keys::ShapesRoot::LOCAL_SCALE].size() >= 3)
                            {
                                const auto& sc = sh_json[Keys::ShapesRoot::LOCAL_SCALE];
                                scaled->m_scale = {
                                    sc[0].is_number() ? sc[0].get<float>() : 1.0f,
                                    sc[1].is_number() ? sc[1].get<float>() : 1.0f,
                                    sc[2].is_number() ? sc[2].get<float>() : 1.0f
                                };
                            }

                            size_t child_idx = SafeGetNumber(sh_json, Keys::ShapesRoot::CHILD_SHAPE_INDEX, static_cast<size_t>(-1));
                            BasicExtractedShape* child_raw = Materialize(child_idx);
                            if (child_raw && child_raw->m_shape_type == BroadphaseNativeTypes::MULTIMATERIAL_TRIANGLE_MESH_PROXYTYPE)
                            {
                                auto cloned_child = CloneExtractedShape(child_raw);
                                scaled->m_internal_triangle_shape.reset(static_cast<MultiMatExtractedShape*>(cloned_child.release()));
                            }
                            shape_lookup[idx] = std::move(scaled);
                            break;
                        }
                        case BroadphaseNativeTypes::COMPOUND_SHAPE_PROXYTYPE:
                        {
                            auto compound = std::make_unique<CompoundExtractedShape>();
                            compound->m_margin = margin;

                            if (sh_json.contains(Keys::ShapesRoot::CHILDREN) && sh_json[Keys::ShapesRoot::CHILDREN].is_array())
                            {
                                for (const auto& child_json : sh_json[Keys::ShapesRoot::CHILDREN])
                                {
                                    if (!child_json.is_object()) continue;

                                    CompoundExtractedShape::Child child;

                                    if (child_json.contains(Keys::ShapesRoot::LOCAL_TRANSFORM) && child_json[Keys::ShapesRoot::LOCAL_TRANSFORM].is_array())
                                    {
                                        const auto& trans = child_json[Keys::ShapesRoot::LOCAL_TRANSFORM];
                                        for (size_t k = 0; k < 16 && k < trans.size(); ++k)
                                        {
                                            if (trans[k].is_number())
                                            {
                                                child.m_local_transform.At(k) = trans[k].get<float>();
                                            }
                                        }
                                    }

                                    size_t child_idx = SafeGetNumber(child_json, Keys::ShapesRoot::CHILD_SHAPE_INDEX, static_cast<size_t>(-1));
                                    BasicExtractedShape* child_raw = Materialize(child_idx);
                                    if (child_raw)
                                    {
                                        child.m_child_ptr = CloneExtractedShape(child_raw);
                                        compound->m_children.push_back(std::move(child));
                                    }
                                }
                            }
                            shape_lookup[idx] = std::move(compound);
                            break;
                        }
                        default:
                            break;
                    }
                }
                catch (...)
                {
                    shape_lookup[idx] = nullptr;
                }

                being_built[idx] = 0;
                return shape_lookup[idx] ? shape_lookup[idx].get() : nullptr;
            };

            for (size_t i = 0; i < shapes_array.size(); ++i)
            {
                Materialize(i);
            }

            std::vector<ExtractedObject> extracted_objects;

            if (manifest_json.contains(Keys::ObjectsRoot::ROOT) && manifest_json[Keys::ObjectsRoot::ROOT].is_array())
            {
                const auto& objects_array = manifest_json[Keys::ObjectsRoot::ROOT];
                extracted_objects.reserve(objects_array.size());

                for (const auto& obj_json : objects_array)
                {
                    try
                    {
                        if (!obj_json.is_object()) continue;

                        ExtractedObject obj;
                        obj.m_collision_object_info.m_collision_flags   = SafeGetNumber(obj_json, Keys::ObjectsRoot::COLLISION_FLAGS, uint32_t(0));
                        obj.m_collision_object_info.m_internal_type     = SafeGetNumber(obj_json, Keys::ObjectsRoot::INTERNAL_TYPE,   int(0));
                        obj.m_collision_object_info.m_mass              = SafeGetNumber(obj_json, Keys::ObjectsRoot::MASS,            float(0.0f));     

                        obj.m_root_shape = nullptr;

                        if (obj_json.contains(Keys::ObjectsRoot::TRANSFORM) && obj_json[Keys::ObjectsRoot::TRANSFORM].is_array())
                        {
                            const auto& trans = obj_json[Keys::ObjectsRoot::TRANSFORM];
                            for (size_t k = 0; k < 16 && k < trans.size(); ++k)
                            {
                                if (trans[k].is_number())
                                {
                                    obj.m_collision_object_info.m_world_transform.At(k) = trans[k].get<float>();
                                }
                            }
                        }

                        size_t shape_index = SafeGetNumber(obj_json, Keys::ObjectsRoot::SHAPE_INDEX, static_cast<size_t>(-1));
                        if (BasicExtractedShape* root_raw = Materialize(shape_index))
                        {
                            obj.m_root_shape = CloneExtractedShape(root_raw);
                        }

                        extracted_objects.push_back(std::move(obj));
                    }
                    catch (...)
                    {
                        continue;
                    }
                }
            }
            return extracted_objects; 
        }

        [[nodiscard]] inline std::vector<ExtractedObject> DeserializeObjectsFromFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) 
            {
                std::cerr << "Failed to deserialize: Could not open file: " << path << std::endl;
                return {};
            }

            const std::streamsize size = file.tellg();
            if (size <= 0)
            {
                std::cerr << "Failed to deserialize: File is empty: " << path << std::endl;
                return {};
            }

            std::vector<char> buffer(static_cast<size_t>(size));
            file.seekg(0, std::ios::beg);

            if (!file.read(buffer.data(), size))
            {
                std::cerr << "Failed to deserialize: Could not read file: " << path << std::endl;
                return {};
            }

            return DeserializeObjectsFromDataBuffer(buffer.data(), buffer.size());
        }

        [[nodiscard]] inline std::string SerializeObjectsToString(const std::vector<CollisionObject*>& objects)
        {
            std::ostringstream oss(std::ios::binary);
            SerializeObjectsToStream(oss, objects);
            return oss.str();
        }

        inline void SerializeObjectsToFile(const std::vector<CollisionObject*>& objects, const std::filesystem::path& path)
        {
            std::ofstream out(path.c_str(), std::ios::binary);
            if (!out.is_open()) 
            {
                std::cerr << "Could not Serialize because of failure to open or create: " << path << std::endl;
                return;
            }

            SerializeObjectsToStream(out, objects);
            out.close();
        }
    }
}
