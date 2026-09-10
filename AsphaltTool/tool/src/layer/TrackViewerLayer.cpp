#include "layer/TrackViewerLayer.h"
#include "common/RacerState.h"
#include "Communication.h"
#include "common/Replay.h"
#include "core/model/CapsuleModel.h"
#include "core/model/Model.h"
#include "core/scene/CameraController.h"
#include "core/scene/Scene3D.h"
#include "glm/geometric.hpp"
#include "layer/GuiStyle.h"
#include "globalstate/AsphaltDllManager.h"
#include "BulletSerializer.h"
#include "BulletTypes.h"

#include "core/application/Application.h"
#include "core/layer/Editor_3D_Layer.h"
#include "core/model/BoxModel.h"
#include "core/model/Light.h"
#include "core/model/Mesh.h"
#include "core/model/PointsModel.h"
#include "core/model/SphereModel.h"
#include "Core/model/CylinderModel.h"
#include "core/model/CapsuleModel.h"
#include "core/rendering/Texture.h"
#include "core/scene/FreeCam_CameraController.h"
#include "core/scene/Scene3D_ObjectBuilder.h"
#include "core/utility/Assert.h"
#include "core/utility/CommonUtility.h"
#include "core/utility/Performance.h"
#include "core/utility/NativeFileDialogue.h"

#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "imgui.h"
#include "imgui/ImGuiFileDialog.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <ranges>
#include <string>

// For triangle mesh extraction
namespace 
{
    struct VertexHasher
    {
        size_t operator()(const CoreEngine::Vertex& v) const 
        {
            size_t h1 = std::hash<float>()(v.m_position.x);
            size_t h2 = std::hash<float>()(v.m_position.y);
            size_t h3 = std::hash<float>()(v.m_position.z);
            size_t h4 = std::hash<float>()(v.m_normal.x);
            return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1) ^ (h4 << 2);
        }
    };

    struct VertexEqual 
    {
        bool operator()(const CoreEngine::Vertex& a, const CoreEngine::Vertex& b) const
        {
            const float epsilon = 0.00001f;
            return glm::all(glm::epsilonEqual(a.m_position, b.m_position, epsilon)) && glm::all(glm::epsilonEqual(a.m_normal, b.m_normal, epsilon));
        }
    };
}

namespace AsphaltTas
{
    TrackViewerLayer::TrackViewerLayer(CoreEngine::Window::Handle handle) noexcept : CoreEngine::Editor_3D_Layer(handle) 
    {
        s_instance = this;

        m_update_scene_physics = false;
        m_draw_bullet_debug = false;
        OnChangeCameraController<CoreEngine::FreeCam_CameraController>();

        CoreEngine::Light light;
        light.m_position   = {0.0f, 0.0f, 0.0f};
        light.m_color      = {1.0f, 1.0f, 1.0f};
        light.m_intensity  = 50.0f;
        light.m_light_mode = CoreEngine::Light::LIGHT_MODE::DIRECT_LIGHT;
        m_scene.EmplaceLightSource(light);

        light.m_position  = {0, 0, 500};
        light.m_intensity = 200.0f;
        m_scene.EmplaceLightSource(light);

        LoadColorDefFromFile(COLOR_DEF_FILE_NAME);

        m_indirect_pipeline.GetRenderConfigRef().m_use_cull_face = false;
        m_draw_lines_pipeline.SetLineThicknessFactor(10.0f);
    }

    TrackViewerLayer::~TrackViewerLayer() noexcept { s_instance = nullptr; } 

    void TrackViewerLayer::OnEvent(CoreEngine::Basic_Event& e) noexcept
    {
        Editor_3D_Layer::OnEvent(e);
    }

    void TrackViewerLayer::OnUpdate(CoreEngine::Units::MicroSecond delta_time) noexcept
    {
        const auto last_dumped_req_id = AsphaltDllManager::GetDllStateOutCopy()->m_meta_data.m_last_completed_dump_request_id;

        if (m_input_state.m_key_is_pressed[GLFW_KEY_ESCAPE] && !m_render_gui) 
        {
            OnSetRenderGUI(true);
        }

        if (last_dumped_req_id == AsphaltDllManager::GetDllGeneralCommandsInRef()->m_write_meta_data.m_dump_track_request_id && m_has_to_move_track_file)
        {
            std::optional<std::string> game_path = AsphaltDllManager::GetGameDirectoryPath();
            if (game_path.has_value())
            {
                std::error_code ec;
                std::filesystem::copy_file(game_path.value() + "/" + Communication::DLL_DUMPED_TRACK_FILE_NAME, m_next_track_path, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec)
                {
                    ENGINE_ERROR_PRINT("Failed to move objects.TRACK file from game directory: FROM: " << game_path.value() + "/" + Communication::DLL_DUMPED_TRACK_FILE_NAME <<
                    " TO: " << m_next_track_path << ec.message());
                }
                else
                {
                    m_has_to_move_track_file = false;
                }
            }
            else 
            {
                ENGINE_ERROR_PRINT("Failed to obtain game directory.");
            }
        }

        Editor_3D_Layer::OnUpdate(delta_time);
    }

    void TrackViewerLayer::OnRender() noexcept 
    {
        Editor_3D_Layer::OnRender();

        if (m_replay_draw_line_pipeline.GetAmountLineVertices() == 0) return;

        m_replay_draw_line_pipeline.SetCameraData(m_camera.GetCameraMatrix());
        m_replay_draw_line_pipeline.Render();
    }

    void TrackViewerLayer::OnImGuiRender() noexcept 
    {
        if (m_render_gui)
        {
            OnImGuiRender_LeftOptionPanel();
            OnImGuiRender_BottomOptionPanel();
            OnImGuiRender_RightOptionPanel();
        }
    }

    void TrackViewerLayer::LoadTrackFromFile(const std::filesystem::path& path) noexcept
    {
        m_selected_object_state.m_object_id = CoreEngine::Scene3D::ObjectID::CreateInvalidID();
        m_show_non_triangle_meshes = true;
        m_scene.ClearAllSceneObjects();
        
        std::vector<BulletTypes::Serializer::ExtractedObject> object_list = BulletTypes::Serializer::DeserializeObjectsFromFile(path);

        const auto ConvertVector3ToGlm = [](const BulletTypes::UnalignedVector3& vec) -> glm::vec3
        {
            return { vec.x, vec.z, -1.0f * vec.y };
        };

        const auto GetPositionFromTransform = [&ConvertVector3ToGlm](const BulletTypes::UnalignedTransform& trans) -> glm::vec3
        {
            return ConvertVector3ToGlm(BulletTypes::UnalignedVector3(trans[3].x, trans[3].y, trans[3].z));
        };

        const auto GetRotationFromTransform = [&ConvertVector3ToGlm](const BulletTypes::UnalignedTransform& trans) -> glm::quat
        {
            BulletTypes::UnalignedVector3 physics_right(trans[0].x, trans[1].x, trans[2].x);
            BulletTypes::UnalignedVector3 physics_forward(trans[0].y, trans[1].y, trans[2].y);
            BulletTypes::UnalignedVector3 physics_up(trans[0].z, trans[1].z, trans[2].z);

            glm::vec3 right   = ConvertVector3ToGlm(physics_right);
            glm::vec3 forward = ConvertVector3ToGlm(physics_forward);
            glm::vec3 up      = ConvertVector3ToGlm(physics_up);

            glm::mat3 basis;
            basis[0] = right;
            basis[1] = up;
            basis[2] = -forward;

            return glm::normalize(glm::quat_cast(basis));
        };

        const auto GetPositionAndRotationFromTrans = [&GetRotationFromTransform, &GetPositionFromTransform](const BulletTypes::UnalignedTransform& trans)
        -> std::pair<glm::vec3, glm::quat>
        {
            return { GetPositionFromTransform(trans), GetRotationFromTransform(trans) };
        };

        struct VerticesIndicesForMaterialID
        {
            int32_t m_material_id;
            std::vector<CoreEngine::Vertex> m_vertices;
            std::vector<GLuint> m_indices;
        };

        const auto MaterialGroupedDataFromBinaryTriangles =
        [&ConvertVector3ToGlm](const std::vector<BulletTypes::Serializer::BinaryTriangle>& triangles)
        {
            struct MaterialBucket
            {
                std::vector<CoreEngine::Vertex> m_vertices;
                std::vector<GLuint> m_indices;
                std::unordered_map<CoreEngine::Vertex, GLuint, VertexHasher, VertexEqual> m_unique_vertices;
            };

            std::unordered_map<int32_t, MaterialBucket> material_buckets;

            for (const auto& triangle : triangles)
            {
                glm::vec3 p0 = ConvertVector3ToGlm(triangle.m_vert_a);
                glm::vec3 p1 = ConvertVector3ToGlm(triangle.m_vert_b);
                glm::vec3 p2 = ConvertVector3ToGlm(triangle.m_vert_c);

                glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
                const float len = glm::length(normal);
                normal = (len > 0.00001f) ? (normal / len) : glm::vec3(0.0f, 1.0f, 0.0f);

                CoreEngine::Vertex tri_verts[3] =
                {
                    { p0, normal, glm::vec2(0.0f) },
                    { p1, normal, glm::vec2(0.0f) },
                    { p2, normal, glm::vec2(0.0f) }
                };

                int32_t material = triangle.m_index_material;
                auto& bucket = material_buckets[material];

                for (int i = 0; i < 3; ++i)
                {
                    auto it = bucket.m_unique_vertices.find(tri_verts[i]);
                    GLuint index;

                    if (it == bucket.m_unique_vertices.end())
                    {
                        index = static_cast<GLuint>(bucket.m_vertices.size());
                        bucket.m_vertices.push_back(tri_verts[i]);
                        bucket.m_unique_vertices.emplace(tri_verts[i], index);
                    }
                    else
                    {
                        index = it->second;
                    }
                    bucket.m_indices.push_back(index);
                }
            }

            std::vector<VerticesIndicesForMaterialID> result;
            result.reserve(material_buckets.size());

            for (auto& [material, bucket] : material_buckets)
            {
                result.push_back(VerticesIndicesForMaterialID{ material, std::move(bucket.m_vertices), std::move(bucket.m_indices) });
            }

            std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
                return a.m_material_id < b.m_material_id;
            });

            return result;
        };

        std::function<void(const BulletTypes::Serializer::BasicExtractedShape*,const BulletTypes::UnalignedTransform&, 
                                 const BulletTypes::Serializer::CollisionObjectInfo&, const std::string&)> RenderShapeAtTransform;

        RenderShapeAtTransform = [&](const BulletTypes::Serializer::BasicExtractedShape* shape, const BulletTypes::UnalignedTransform& raw_transform, 
                                     const BulletTypes::Serializer::CollisionObjectInfo& info, const std::string& name_prefix)
        {
            if (!shape) return;

            auto [position, rotation] = GetPositionAndRotationFromTrans(raw_transform);

            switch (shape->m_shape_type)
            {
                case BulletTypes::BroadphaseNativeTypes::BOX_SHAPE_PROXYTYPE:
                {
                    const auto* box_shape = static_cast<const BulletTypes::Serializer::BoxExtractedShape*>(shape);
                    const glm::vec3 half_extents = ConvertVector3ToGlm(box_shape->m_implicit_shape_dimensions);

                    std::stringstream ss;
                    ss << name_prefix << "Box" 
                    << "\nCollision Flags: " << info.m_collision_flags
                    << "\nInternalType: " << info.m_internal_type
                    << "\nMass: " << info.m_mass
                    << "\nTransform Rot: " << raw_transform.m_basis->ToString()
                    << "\nTransform Pos: " << raw_transform.m_origin.ToString();

                    const glm::vec3 color = m_color_defs[COLOR_DEFS_INDEX_BOX];

                    CoreEngine::Scene3D_ObjectBuilder builder = m_scene.CreateObjectBuilder();
                    builder.RenderModel_SetExisting(std::make_unique<CoreEngine::BoxModel>(half_extents, position, rotation, color));
                    builder.SetPosition(position);
                    builder.SetRotation(rotation);
                    builder.SetName(ss.str());
                    m_scene.AddObjectFromBuilder(std::move(builder)); 
                    return;
                }

                case BulletTypes::BroadphaseNativeTypes::SPHERE_SHAPE_PROXYTYPE:
                {
                    const auto* sphere_shape = static_cast<const BulletTypes::Serializer::SphereExtractedShape*>(shape);
                    const glm::vec3 half_extents = ConvertVector3ToGlm(sphere_shape->m_implicit_shape_dimensions);

                    std::stringstream ss;
                    ss << name_prefix << "Sphere"
                    << "\nCollision Flags: " << info.m_collision_flags
                    << "\nInternalType: " << info.m_internal_type
                    << "\nMass: " << info.m_mass
                    << "\nTransform Rot: " << raw_transform.m_basis->ToString()
                    << "\nTransform Pos: " << raw_transform.m_origin.ToString();
                    
                    const glm::vec3 color = m_color_defs[COLOR_DEFS_INDEX_SPHERE];

                    CoreEngine::Scene3D_ObjectBuilder builder = m_scene.CreateObjectBuilder();
                    builder.RenderModel_SetExisting(std::make_unique<CoreEngine::SphereModel>(half_extents.x, position, rotation, color));
                    builder.SetPosition(position);
                    builder.SetRotation(rotation);
                    builder.SetName(ss.str());
                    m_scene.AddObjectFromBuilder(std::move(builder));
                    return; 
                }

                case BulletTypes::BroadphaseNativeTypes::CAPSULE_SHAPE_PROXYTYPE:
                {
                    const auto* capsule_shape = static_cast<const BulletTypes::Serializer::CapsuleExtractedShape*>(shape);
                    const glm::vec3 implicit_dims = ConvertVector3ToGlm(capsule_shape->m_implicit_shape_dimensions);
                    const int up_axis = capsule_shape->m_up_axis;

                    float radius = 0.0f;
                    float half_height = 0.0f;
                    glm::quat mesh_alignment = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

                    if (up_axis == 0)
                    {
                        radius         = implicit_dims.y;
                        half_height    = implicit_dims.x;
                    }
                    else if (up_axis == 1)
                    {
                        radius = implicit_dims.x;
                        half_height = implicit_dims.y;
                    }
                    else if (up_axis == 2)
                    {
                        radius         = implicit_dims.x;
                        half_height    = implicit_dims.z;
                    }
                    const float cylinder_height = half_height * 2.0f; 

                    std::stringstream ss;
                    ss << name_prefix << "Capsule"
                       << "\nCollision Flags: " << info.m_collision_flags
                       << "\nInternalType: " << info.m_internal_type
                       << "\nMass: " << info.m_mass
                       << "\nTransform Rot: " << raw_transform.m_basis->ToString()
                       << "\nTransform Pos: " << raw_transform.m_origin.ToString();
                    
                    const glm::vec3 color = m_color_defs[COLOR_DEFS_INDEX_CAPSULE];

                    CoreEngine::Scene3D_ObjectBuilder builder = m_scene.CreateObjectBuilder();
                    builder.RenderModel_SetExisting(std::make_unique<CoreEngine::CapsuleModel>(radius, cylinder_height, position, rotation, color));
                    
                    builder.SetPosition(position);
                    builder.SetRotation(rotation);
                    builder.SetName(ss.str());
                    m_scene.AddObjectFromBuilder(std::move(builder));
                    return; 
                }

                case BulletTypes::BroadphaseNativeTypes::CYLINDER_SHAPE_PROXYTYPE:
                {
                    const auto* cylinder_shape = static_cast<const BulletTypes::Serializer::CylinderExtractedShape*>(shape);
                    const glm::vec3 implicit_dims = ConvertVector3ToGlm(cylinder_shape->m_implicit_shape_dimensions);
                    const int up_axis = cylinder_shape->m_up_axis;

                    float radius = 0.0f;
                    float half_height = 0.0f;
                    glm::quat mesh_alignment = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

                    if (up_axis == 0)
                    {
                        radius         = implicit_dims.y; 
                        half_height    = implicit_dims.x;
                    }
                    else if (up_axis == 1)
                    {
                        radius      = implicit_dims.x;
                        half_height = implicit_dims.y;
                    }
                    else if (up_axis == 2)
                    {
                        radius         = implicit_dims.x;
                        half_height    = implicit_dims.z;
                    }

                    const float total_height = half_height * 2.0f; 

                    std::stringstream ss;
                    ss << name_prefix << "Cylinder"
                       << "\nCollision Flags: " << info.m_collision_flags
                       << "\nInternalType: " << info.m_internal_type
                       << "\nMass: " << info.m_mass
                       << "\nTransform Rot: " << raw_transform.m_basis->ToString()
                       << "\nTransform Pos: " << raw_transform.m_origin.ToString();
                    
                    const glm::vec3 color = m_color_defs[COLOR_DEFS_INDEX_CYLINDER]; 

                    CoreEngine::Scene3D_ObjectBuilder builder = m_scene.CreateObjectBuilder();
                    
                    builder.RenderModel_SetExisting(std::make_unique<CoreEngine::CylinderModel>(radius, total_height, up_axis, position, rotation, color));
                    
                    builder.SetPosition(position);
                    builder.SetRotation(rotation); 
                    builder.SetName(ss.str());
                    m_scene.AddObjectFromBuilder(std::move(builder));
                    return; 
                }

                case BulletTypes::BroadphaseNativeTypes::MULTIMATERIAL_TRIANGLE_MESH_PROXYTYPE:
                {
                    const auto* multimat = static_cast<const BulletTypes::Serializer::MultiMatExtractedShape*>(shape);
                    std::vector<VerticesIndicesForMaterialID> data_per_material_mesh = MaterialGroupedDataFromBinaryTriangles(multimat->m_triangles);

                    std::stringstream ss;
                    ss << name_prefix << "MultiMat"
                    << "\nCollision Flags: " << info.m_collision_flags
                    << "\nInternalType: " << info.m_internal_type
                    << "\nMass: " << info.m_mass
                    << "\nTransform Rot: " << raw_transform.m_basis->ToString()
                    << "\nTransform Pos: " << raw_transform.m_origin.ToString()
                    << "\nAmount Triangles: " << multimat->m_triangles.size();

                    const bool is_main_map_shape = (multimat->m_triangles.size() > 5000);

                    std::vector<CoreEngine::Mesh> meshes;
                    meshes.reserve(data_per_material_mesh.size());
                    for (auto& mesh_data : data_per_material_mesh)
                    {
                        std::shared_ptr<CoreEngine::MaterialPBR> pbr_material = std::make_shared<CoreEngine::MaterialPBR>();
                        glm::vec3 color;
                        if (is_main_map_shape)
                        {
                            const size_t index = COLOR_DEFS_BEGIN_MATERIALS + mesh_data.m_material_id;
                            color = index >= m_color_defs.size() ? m_color_defs.back() : m_color_defs[index];
                        }
                        else 
                        {
                            color = m_color_defs[COLOR_DEFS_INDEX_DYNAMICS];
                        }
                        pbr_material->m_base_texture = std::make_shared<CoreEngine::Texture>(color);
                        meshes.emplace_back(std::move(mesh_data.m_vertices), std::move(mesh_data.m_indices), std::move(pbr_material));
                    }

                    CoreEngine::Scene3D_ObjectBuilder builder = m_scene.CreateObjectBuilder();
                    builder.RenderModel_SetExisting(std::make_unique<CoreEngine::PointsModel>(std::move(meshes), position, rotation, glm::vec3{1.0f}));
                    builder.SetPosition(position);
                    builder.SetRotation(rotation);
                    builder.SetName(ss.str());
                    m_scene.AddObjectFromBuilder(std::move(builder));
                    return;
                } 

                case BulletTypes::BroadphaseNativeTypes::SCALED_TRIANGLE_MESH_SHAPE_PROXYTYPE:
                {
                    const auto* scaled = static_cast<const BulletTypes::Serializer::ScaledTriangleMeshExtractedShape*>(shape);
                    if (!scaled->m_internal_triangle_shape ||
                        scaled->m_internal_triangle_shape->m_shape_type != BulletTypes::BroadphaseNativeTypes::MULTIMATERIAL_TRIANGLE_MESH_PROXYTYPE)
                    {
                        ENGINE_ERROR_PRINT("Assumption incorrect: Scaled Triangle Mesh with no internal MultiMaterialTriangle mesh used!");
                        return;
                    }
                    const auto* inner_shape = scaled->m_internal_triangle_shape.get();
                    const glm::vec3 scale = {scaled->m_scale.x, scaled->m_scale.z, scaled->m_scale.y}; // Do NOT invert scale

                    std::vector<VerticesIndicesForMaterialID> data_per_material_mesh = MaterialGroupedDataFromBinaryTriangles(inner_shape->m_triangles);
                    std::stringstream ss;
                    ss << name_prefix << "ScaledMultiMat"
                    << "\nCollision Flags: " << info.m_collision_flags
                    << "\nInternalType: " << info.m_internal_type
                    << "\nMass: " << info.m_mass
                    << "\nTransform Rot: " << raw_transform.m_basis->ToString()
                    << "\nTransform Pos: " << raw_transform.m_origin.ToString()
                    << "\nAmount Triangles: " << inner_shape->m_triangles.size();

                    std::vector<CoreEngine::Mesh> meshes;
                    meshes.reserve(data_per_material_mesh.size());
                    for (auto& mesh_data : data_per_material_mesh)
                    {
                        std::shared_ptr<CoreEngine::MaterialPBR> pbr_material = std::make_shared<CoreEngine::MaterialPBR>();
                        pbr_material->m_base_texture = std::make_shared<CoreEngine::Texture>(m_color_defs[COLOR_DEFS_INDEX_RAMPS]);
                        meshes.emplace_back(std::move(mesh_data.m_vertices), std::move(mesh_data.m_indices), std::move(pbr_material));
                    }

                    std::unique_ptr<CoreEngine::PointsModel> model = std::make_unique<CoreEngine::PointsModel>(std::move(meshes), position, rotation, glm::vec3{1.0f});
                    model->SetScale(scale);

                    CoreEngine::Scene3D_ObjectBuilder builder = m_scene.CreateObjectBuilder();
                    builder.RenderModel_SetExisting(std::move(model));
                    builder.SetPosition(position);
                    builder.SetRotation(rotation);
                    builder.SetName(ss.str());
                    m_scene.AddObjectFromBuilder(std::move(builder));
                    return;
                }

                case BulletTypes::BroadphaseNativeTypes::COMPOUND_SHAPE_PROXYTYPE:
                {
                    const auto* compound = static_cast<const BulletTypes::Serializer::CompoundExtractedShape*>(shape);

                    for (const auto& child : compound->m_children)
                    {
                        if (!child.m_child_ptr) continue;

                        const BulletTypes::UnalignedTransform combined = BulletTypes::Custom::ComposeBulletTransforms(raw_transform, child.m_local_transform);

                        RenderShapeAtTransform(child.m_child_ptr.get(), combined, info, name_prefix + " [Compound Child] ");
                    }
                    return;
                } 

                default:
                    return;
            }
        };

        for (const BulletTypes::Serializer::ExtractedObject& object : object_list)
        {
            if (!object.m_root_shape) continue;

            RenderShapeAtTransform(object.m_root_shape.get(), object.m_collision_object_info.m_world_transform, object.m_collision_object_info, "");
        }

        m_indirect_pipeline.SetSceneData(m_scene.GetRenderModelVector(), m_scene.GetLightVectorConstRef());
    }

    void TrackViewerLayer::OnImGuiRender_LeftOptionPanel() noexcept
    {
        ImGuiIO& io = ImGui::GetIO();

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | 
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, BORDER_SIZE_PIXELS);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        ImGui::PushStyleColor(ImGuiCol_Border, COLOR_BORDER);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, COLOR_BG);
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip,        COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,  COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_Text, COLOR_TEXT);

        const float top_bar_height = io.DisplaySize.y * TOP_BAR_HEIGHT_RELATIVE;
        const float panel_height   = io.DisplaySize.y - top_bar_height;
        ImGui::SetNextWindowPos(ImVec2(0, top_bar_height));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * SIDE_PANEL_DEFAULT_WIDTH_RELATIVE, panel_height), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(io.DisplaySize.x * SIDE_PANEL_MIN_WIDTH_RELATIVE, panel_height), ImVec2(io.DisplaySize.x * SIDE_PANEL_MAX_WIDTH_RELATIVE, panel_height));

        ImGui::Begin("Editor_3D_LeftOptionPanel", nullptr, window_flags);

        if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            // Deprecated in favor of native file dialogue
            /*{
                PUSH_SCOPED_STYLE_COLOR(ImGuiCol_Button, COLOR_GREEN);
                if (ImGui::Button("Load Track##sceneload"))
                {
                    ImGuiFileDialog::Instance()->OpenDialog("LoadTrackKey", "Load Serialized Track", ".TRACK");
                } 
            }

            {
                PUSH_SCOPED_STYLE_COLOR(ImGuiCol_Button, GuiStyle::COLOR_GREEN);
                if (ImGui::Button("Export Ingame Track"))
                {
                    ImGuiFileDialog::Instance()->OpenDialog("ExportIngameTrackKey", "Export current Track", ".TRACK");
                }
            }

            {
                PUSH_SCOPED_STYLE_COLOR(ImGuiCol_Button, GuiStyle::COLOR_GREEN);
                if (ImGui::Button("Load Replay"))
                {
                    ImGuiFileDialog::Instance()->OpenDialog("LoadReplayForTrackViewKey", "Load Replay", Communication::REPLAY_FILE_TYPE.c_str());
                }
            }

            if (ImGuiFileDialog::Instance()->Display("LoadTrackKey"))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    LoadTrackFromFile(ImGuiFileDialog::Instance()->GetFilePathName());
                }
                ImGuiFileDialog::Instance()->Close();
            }

            if (ImGuiFileDialog::Instance()->Display("ExportIngameTrackKey"))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    m_next_track_path = ImGuiFileDialog::Instance()->GetFilePathName();
                    auto ref = AsphaltDllManager::GetDllGeneralCommandsInRef(); 
                    auto cur_id = AsphaltDllManager::GetDllStateOutCopy()->m_meta_data.m_last_completed_dump_request_id;
                    ref->m_write_meta_data.m_dump_track_request_id = cur_id + 1;
                    ref->m_write_meta_data.m_command_type = ComDllIn::CommandType::ExecuteCommand;
                    m_has_to_move_track_file = true;
                }
                ImGuiFileDialog::Instance()->Close();
            }

            if (ImGuiFileDialog::Instance()->Display("LoadReplayForTrackViewKey"))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    Replay replay = Replay::DeserializeReplayFromFile(ImGuiFileDialog::Instance()->GetFilePathName());
                    if (replay.IsValid() && replay.GetAmountFrames() > 0) 
                    {
                        LoadReplay(std::move(replay));
                    }
                }
                ImGuiFileDialog::Instance()->Close();
            } */

            {
                PUSH_SCOPED_STYLE_COLOR(ImGuiCol_Button, COLOR_GREEN);
                if (ImGui::Button("Load Track##sceneload"))
                {
                    CoreEngine::NativeFileDialogue::OpenDialogueConfig config;
                    config.m_dialogue_name = L"Load Track";
                    config.m_filter_name   = L"Load Track";
                    config.m_filter_spec   = Communication::TRACK_FILE_TYPE_WITH_STAR_WIDE;
                    config.m_main_window_handle = CoreEngine::Application::Get()->GetWindowPtr(m_handle)->GetHWND();
                    CoreEngine::NativeFileDialogue::OpenFile(config, [this](CoreEngine::NativeFileDialogue::DialogueResult result)
                    {
                        if (result.IsSuccess())
                        {
                            LoadTrackFromFile(result.GetPath());
                        }
                    });
                } 
            }

            {
                PUSH_SCOPED_STYLE_COLOR(ImGuiCol_Button, GuiStyle::COLOR_GREEN);
                if (ImGui::Button("Export Ingame Track"))
                {
                    CoreEngine::NativeFileDialogue::CreateDialogueConfig config;
                    config.m_dialogue_name      = L"Export Ingame Track";
                    config.m_extension          = Communication::TRACK_FILE_TYPE_NO_DOT_WIDE;
                    config.m_filter_spec        = Communication::TRACK_FILE_TYPE_WITH_STAR_WIDE;
                    config.m_main_window_handle = CoreEngine::Application::Get()->GetWindowPtr(m_handle)->GetHWND();
                    CoreEngine::NativeFileDialogue::CreateNewFile(config, [this](CoreEngine::NativeFileDialogue::DialogueResult result)
                    {
                        if (result.IsSuccess())
                        {
                            m_next_track_path = result.GetPath();
                            auto ref = AsphaltDllManager::GetDllGeneralCommandsInRef(); 
                            auto cur_id = AsphaltDllManager::GetDllStateOutCopy()->m_meta_data.m_last_completed_dump_request_id;
                            ref->m_write_meta_data.m_dump_track_request_id = cur_id + 1;
                            ref->m_write_meta_data.m_command_type = ComDllIn::CommandType::ExecuteCommand;
                            m_has_to_move_track_file = true;
                        }
                    });
                }
            }

            {
                PUSH_SCOPED_STYLE_COLOR(ImGuiCol_Button, COLOR_GREEN);
                if (ImGui::Button("Load Replay##loadreplaybutton"))
                {
                    CoreEngine::NativeFileDialogue::OpenDialogueConfig config;
                    config.m_dialogue_name = L"Load Replay";
                    config.m_filter_name   = L"Load Replay";
                    config.m_filter_spec   = Communication::REPLAY_FILE_TYPE_WITH_STAR_WIDE;
                    config.m_main_window_handle = CoreEngine::Application::Get()->GetWindowPtr(m_handle)->GetHWND();
                    CoreEngine::NativeFileDialogue::OpenFile(config, [this](CoreEngine::NativeFileDialogue::DialogueResult result)
                    {
                        if (result.IsSuccess())
                        {
                            LoadReplay(Replay::DeserializeReplayFromFile(result.GetPath()));
                        }
                    });
                }
            }

            ////////////////////

            {
                PUSH_SCOPED_STYLE_COLOR(ImGuiCol_Button, GuiStyle::COLOR_RED);
                ImGui::SameLine();
                if (ImGui::Button("Clear Replay"))
                {
                    LoadReplay(std::nullopt);
                }
            }

            if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ORANGE);
                ImGui::Text("Objects: %zu", m_scene.GetAmountObjects());
                ImGui::PopStyleColor();

                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader("Lights##lightsheader", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ORANGE);
                ImGui::Text("Lights: %zu", m_scene.GetLightVectorConstRef().size());
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Button, COLOR_GREEN);
                if (ImGui::Button("Create##lightsource"))
                {
                    m_object_creation_flags |= ObjectCreationFlag::LIGHT_SOURCE;
                }
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, COLOR_RED);
                if (ImGui::Button("Clear All##lightsclear"))
                {
                    m_scene.ClearAllLightSources();
                }
                ImGui::PopStyleColor();

                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ENGINE_ASSERT(m_camera_controller && "Camera Controller must not be null.");
                ImGui::Indent();

                //////////////////////////////////////////////// 
                //---------  Select desired camera controller / Free cam if no selected object
                //////////////////////////////////////////////// 
                auto selected_obj_ptr = m_scene.GetSceneObject(m_selected_object_state.m_object_id);
                if (selected_obj_ptr)
                {
                    int cam_controller = static_cast<int>(m_camera_controller->GetType());
                    ImGui::RadioButton("Free Cam", &cam_controller, static_cast<int>(CoreEngine::Basic_CameraController::Type::FreeCam));
                    ImGui::SameLine();
                    ImGui::RadioButton("Orbital" , &cam_controller, static_cast<int>(CoreEngine::Basic_CameraController::Type::OrbitalCam)); 
                    ImGui::SameLine();
                    ImGui::RadioButton("Follow"  , &cam_controller, static_cast<int>(CoreEngine::Basic_CameraController::Type::FollowCam));
                    if (cam_controller != static_cast<int>(m_camera_controller->GetType()))
                    {
                        switch (static_cast<CoreEngine::Basic_CameraController::Type>(cam_controller))
                        {
                            case CoreEngine::Basic_CameraController::Type::FreeCam   : OnChangeCameraController<CoreEngine::FreeCam_CameraController>();    break;
                            case CoreEngine::Basic_CameraController::Type::OrbitalCam: OnChangeCameraController<CoreEngine::OrbitalCam_CameraController>(); break;
                            case CoreEngine::Basic_CameraController::Type::FollowCam : OnChangeCameraController<CoreEngine::FollowCam_CameraController>();  break;
                            default: ENGINE_ASSERT(false && "Expected a valid camera type to be selected.");
                        }
                    }
                }

                //////////////////////////////////////////////// 
                //--------- Options for the selected controller
                //////////////////////////////////////////////// 
                if (m_camera_controller->GetType() == CoreEngine::Basic_CameraController::Type::FreeCam)
                {
                    CoreEngine::FreeCam_CameraController* controller = static_cast<CoreEngine::FreeCam_CameraController*>(m_camera_controller.get());
                    float speed = controller->GetMoveSpeed();
                    ImGui::SliderFloat(":Speed ", &speed, 0.1f, MAX_CAMERA_SPEED);
                    controller->SetMoveSpeed(speed);
                    float sens = controller->GetSensitivity();
                    ImGui::SliderFloat(":Sensitivity", &sens, 0.05f, 0.5f);
                    controller->SetSensitivity(sens);
                    bool only_look_if_mouse = controller->GetOnlyLookAroundIfRightMouse();
                    if (ImGui::Checkbox("Right Mouse to Look", &only_look_if_mouse))
                    {
                        controller->SetOnlyLookAroundIfRightMouse(only_look_if_mouse);
                    }
                }

                if (m_camera_controller->GetType() == CoreEngine::Basic_CameraController::Type::OrbitalCam)
                {
                    CoreEngine::OrbitalCam_CameraController* controller = static_cast<CoreEngine::OrbitalCam_CameraController*>(m_camera_controller.get());
                    float distance = controller->GetDistance();
                    ImGui::DragFloat(":Distance", &distance, 1.0f, 0.1f, 10'000.0f);
                    controller->SetDistance(distance);
                }

                if (m_camera_controller->GetType() == CoreEngine::Basic_CameraController::Type::FollowCam)
                {
                    CoreEngine::FollowCam_CameraController* controller = static_cast<CoreEngine::FollowCam_CameraController*>(m_camera_controller.get());
                    float distance = controller->GetDistance();
                    ImGui::DragFloat(":Distance", &distance, 1.0f, 0.1f, 10'000.0f);
                    controller->SetDistance(distance);
                }

                float fov = m_camera.GetFovDeg();
                ImGui::SliderFloat(":Fov ", &fov, MIN_FOV_DEGREES, MAX_FOV_DEGREES);
                m_camera.SetFovDeg(fov);

                ImGui::TextUnformatted( m_camera.ToString().c_str() );

                ImGui::Unindent();
            }

            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            if (ImGui::Checkbox("Render GUI", &m_render_gui))
            {
                OnSetRenderGUI(m_render_gui);
            } 

            if (ImGui::Checkbox("Show Boxes", &m_show_non_triangle_meshes))
            {
                OnShowNonTrianglemeshObjects(m_show_non_triangle_meshes);
            }

            bool vsync_now = CoreEngine::Application::Get()->GetVsyncIsOn();
            if (ImGui::Checkbox("Toggle Vsync", &vsync_now))
            {
               CoreEngine:: Application::Get()->SetVsync(vsync_now);
            }

            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Performance"))
        {
            ImGui::Indent();

            ImGui::Text("FPS: %.0f", io.Framerate);

            ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ORANGE);
            ImGui::TextUnformatted("Logged Frametimes:");
            ImGui::PopStyleColor();

            CoreEngine::PerFrameScopeTimes::SortData();
            const auto& scope_times = CoreEngine::PerFrameScopeTimes::GetScopeTimeDataConstRef();
            for (auto rit = scope_times.rbegin(); rit != scope_times.rend(); ++rit)
            {
                ImGui::TextUnformatted(rit->ToString().c_str());
            }

            ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ORANGE);
            ImGui::TextUnformatted("Logged Occurences:");
            ImGui::PopStyleColor();

            const auto& occurence_counts = CoreEngine::PerFrameOccurrenceCounter::GetOccurrenceCounterDataConstRef();
            for (auto rit = occurence_counts.rbegin(); rit != occurence_counts.rend(); ++rit)
            {
                ImGui::TextUnformatted(rit->ToString().c_str());
            }

            ImGui::Unindent();
        }

        m_left_panel_width_relative = ImGui::GetWindowSize().x  / io.DisplaySize.x;
        ImGui::End();

        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);
    }

    void TrackViewerLayer::LoadColorDefFromFile(const std::filesystem::path& path) noexcept
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return;

        const auto size = file.tellg();
        if (size <= 0) return;

        std::string buffer(static_cast<size_t>(size), '\0');

        file.seekg(0, std::ios::beg);
        if (!file.read(buffer.data(), size)) return;

        std::vector<glm::vec3> data;
        size_t begin_bracket_offset = {};
        while (true)
        {
            const auto begin = buffer.find("{", begin_bracket_offset);
            if (begin == std::string::npos) break;
            begin_bracket_offset = begin + 1;

            const auto end = buffer.find("}", begin);
            if (end == std::string::npos) break;

            const auto float_one_end = buffer.find(",", begin + 1);
            if (float_one_end == std::string::npos) break;

            const auto float_two_end = buffer.find(",", float_one_end + 1);
            if (float_two_end == std::string::npos) break;

            try
            {
                const float r = std::stof(buffer.substr(begin         + 1, float_one_end - begin         - 1));
                const float g = std::stof(buffer.substr(float_one_end + 1, float_two_end - float_one_end - 1));
                const float b = std::stof(buffer.substr(float_two_end + 1, end           - float_two_end - 1));
                data.emplace_back(r, g, b);
            }
            catch (...)
            {
                break;
            }
        }
        if (data.size() < 5)
        {
            ENGINE_ERROR_PRINT("Malformed trackview.COLORDEF ignored: Provide at least 5 colors for Ramps, Dynamics, Box, Sphere, Unkown");
            return;
        }
        data.shrink_to_fit();
        m_color_defs = std::move(data);
    }

    void TrackViewerLayer::LoadReplay(std::optional<Replay> replay) noexcept
    {
        m_current_replay = replay;

        const auto replay_path = [&replay]() -> std::vector<CoreEngine::DrawLines3D_RenderPipeline::LineVertex> 
        {
            if (!replay.has_value()) return {};

            const std::vector<Replay::Frame>& frames = replay->GetFrameVectorConstReference();
            std::vector<CoreEngine::DrawLines3D_RenderPipeline::LineVertex> out;
            out.reserve(frames.size() * 2);

            const auto HSVtoRGB = [](float h, float s, float v) -> glm::vec3 
            {
                h = fmod(h, 360.0f);
                float c = v * s;
                float x = c * (1 - fabs(fmod(h / 60.0f, 2) - 1));
                float m = v - c;
                glm::vec3 rgb;
                if (h < 60)       rgb = {c, x, 0};
                else if (h < 120) rgb = {x, c, 0};
                else if (h < 180) rgb = {0, c, x};
                else if (h < 240) rgb = {0, x, c};
                else if (h < 300) rgb = {x, 0, c};
                else              rgb = {c, 0, x};
                return rgb + glm::vec3(m);
            }; 

            float rainbow_length = 60.0f;
            float distance_accum = 0.0f;

            for (size_t i = 0; i < frames.size() - 1; ++i)
            {
                const glm::vec3 p0 = RacerState(frames[i].m_recorded_racer_state).GetPositionOpenGL_XYZ();
                const glm::vec3 p1 = RacerState(frames[i+1].m_recorded_racer_state).GetPositionOpenGL_XYZ();

                bool respawned = glm::distance(p0, p1) > 10.0f;

                /*const float segment_length = glm::length(p1 - p0);
                distance_accum += segment_length;

                const float hue0 = fmod(distance_accum / rainbow_length * 360.0f, 360.0f);
                const float hue1 = fmod((distance_accum + segment_length) / rainbow_length * 360.0f, 360.0f);

                const glm::vec3 color0 = HSVtoRGB(hue0, 1.0f, 1.0f); 
                const glm::vec3 color1 = HSVtoRGB(hue1, 1.0f, 1.0f); */

                const glm::vec3 color = respawned ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{1.0f};

                out.emplace_back(p0, color);
                out.emplace_back(p1, color);
            }

            return out;
        }();

        m_replay_draw_line_pipeline.SetLineData(std::move(replay_path));
        m_replay_draw_line_pipeline.SetLineThicknessFactor(100.0f);
    }

    void TrackViewerLayer::OnImGuiRender_RightOptionPanel() noexcept
    { 
        auto selected_obj_ptr = m_scene.GetSceneObject(m_selected_object_state.m_object_id);
        if (!selected_obj_ptr && (m_object_creation_flags == ObjectCreationFlag::NONE) )
        {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();

        //------------ Scaling hell
        const float bottom_height_px = io.DisplaySize.y * m_bottom_panel_height_relative;

        const float top_bar_height_px   = io.DisplaySize.y * TOP_BAR_HEIGHT_RELATIVE;
        const float available_height_px = io.DisplaySize.y - top_bar_height_px - bottom_height_px;

        const float default_width_px = io.DisplaySize.x * SIDE_PANEL_DEFAULT_WIDTH_RELATIVE;
        const float min_width_px     = io.DisplaySize.x * SIDE_PANEL_MIN_WIDTH_RELATIVE;
        const float max_width_px     = io.DisplaySize.x * SIDE_PANEL_MAX_WIDTH_RELATIVE;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, BORDER_SIZE_PIXELS);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        ImGui::PushStyleColor(ImGuiCol_Border, COLOR_BORDER);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, COLOR_BG);
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip,        COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,  COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_Text, COLOR_TEXT);

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - default_width_px, top_bar_height_px), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(default_width_px, available_height_px), ImGuiCond_Appearing);

        ImGui::SetNextWindowSizeConstraints(ImVec2(min_width_px, available_height_px), ImVec2(max_width_px, available_height_px));

        //------------ 
        ImGui::Begin("Editor_3D_RightOptionPanel", nullptr, window_flags);

        ImGui::SetWindowPos(ImVec2(io.DisplaySize.x - ImGui::GetWindowWidth(), top_bar_height_px));
        ImGui::SetWindowSize(ImVec2(ImGui::GetWindowWidth(), available_height_px));

        //------------ Begin actual options
        if (m_object_creation_flags != ObjectCreationFlag::NONE && ImGui::CollapsingHeader("Object Creation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();
 
            ImGui::PushID(5);
            if (ObjectCreation_HasFlag(m_object_creation_flags, ObjectCreationFlag::LIGHT_SOURCE) && ImGui::CollapsingHeader("Creating LIGHT", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::InputFloat3("Position", &m_light_source_creation_data.m_position.x);
                if (ImGui::Button("Set Camera Position"))
                {
                    m_light_source_creation_data.m_position = m_camera.GetPosition();
                }
                ImGui::SliderFloat3("Color", &m_light_source_creation_data.m_color.x, 0.0f, 1.0f);

                int mode = m_light_source_creation_data.m_light_mode;
                ImGui::SliderInt("Mode", &mode, CoreEngine::Light::LIGHT_MODE::FIRST, CoreEngine::Light::LIGHT_MODE::LAST, CoreEngine::Light::LightModeToString(mode).c_str());
                m_light_source_creation_data.m_light_mode = mode;

                ImGui::SliderFloat("Intensity", &m_light_source_creation_data.m_intensity, 0.0f, 1000.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, COLOR_GREEN);
                if (ImGui::Button("Create"))
                {
                    m_scene.EmplaceLightSource(m_light_source_creation_data);
                }
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, COLOR_RED);
                if (ImGui::Button("Cancel"))
                {
                    ObjectCreation_RemoveFlag(m_object_creation_flags, ObjectCreationFlag::LIGHT_SOURCE);
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
            
            ImGui::Unindent();
        }

        ImGui::PushID(100);
        if (selected_obj_ptr && ImGui::CollapsingHeader("Selected Object", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ORANGE);
            ImGui::TextUnformatted(("Object: " + selected_obj_ptr->m_name).c_str());
            ImGui::PopStyleColor();

            if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                //--------- Position move
                const float delta_time_secs = CoreEngine::Application::Get()->GetLastFrameTime().ConvertTo<CoreEngine::Units::Second>().Get();
                const float MOVE_SPEED = 0.5 * selected_obj_ptr->GetWorldSpaceMaxBoundingSphereRadius();

                ImGui::TextUnformatted("Position");

                glm::vec3 position = selected_obj_ptr->GetPosition();

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_X_AXYS);
                if (ImGui::ArrowButton("##xleft", ImGuiDir_Left) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) 
                { 
                    position.x -= MOVE_SPEED * delta_time_secs; 
                }
                ImGui::SameLine();
                ImGui::Text("%10.2f", position.x);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##xright", ImGuiDir_Right) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) 
                { 
                    position.x += MOVE_SPEED * delta_time_secs; 
                }
                ImGui::SameLine(); 
                ImGui::TextUnformatted("X");
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_Y_AXYS);
                if (ImGui::ArrowButton("##yleft", ImGuiDir_Left) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) 
                { 
                    position.y -= MOVE_SPEED * delta_time_secs; 
                }
                ImGui::SameLine();
                ImGui::Text("%10.2f", position.y);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##yright", ImGuiDir_Right) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) 
                { 
                    position.y += MOVE_SPEED * delta_time_secs; 
                }
                ImGui::SameLine(); 
                ImGui::TextUnformatted("Y");
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_Z_AXYS);
                if (ImGui::ArrowButton("##zleft", ImGuiDir_Left) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) 
                { 
                    position.z -= MOVE_SPEED * delta_time_secs; 
                }
                ImGui::SameLine();
                ImGui::Text("%10.2f", position.z);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##zright", ImGuiDir_Right) || (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) 
                { 
                    position.z += MOVE_SPEED * delta_time_secs; 
                }
                ImGui::SameLine(); 
                ImGui::TextUnformatted("Z");
                ImGui::PopStyleColor();

                //Type position to move to
                if (ImGui::Button("Move to"))
                {
                    position = m_gui_state_selected_object_input_position;
                }
                ImGui::SameLine();
                ImGui::InputFloat3("##position_inputfloat3", &m_gui_state_selected_object_input_position.x);

                if (ImGui::Button("Move to Camera"))
                {
                    position = m_camera.GetPosition();
                }

                if (position != selected_obj_ptr->GetPosition())
                {
                    selected_obj_ptr->SetPosition(position);
                }

                //--------- Rotation
                ImGui::TextUnformatted("Rotation");
                const glm::quat rot_quat = selected_obj_ptr->GetRotation();
                glm::vec3 rot_euler_degrees = glm::degrees(glm::eulerAngles(rot_quat));

                float delta_x = 0.0f, delta_y = 0.0f, delta_z = 0.0f;

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_X_AXYS);
                if (ImGui::DragFloat("##rotx", &delta_x, 0.5f, 0.0f, 0.0f, std::format("X: {:8.2f}°", rot_euler_degrees.x).c_str()))
                {
                    glm::quat delta = glm::angleAxis(glm::radians(delta_x), glm::vec3(1, 0, 0));
                    #ifndef __INTELLISENSE__ //Shut the fuck up intellisense
                    selected_obj_ptr->SetRotation(rot_quat * delta);
                    #endif
                }
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_Y_AXYS);
                if (ImGui::DragFloat("##roty", &delta_y, 0.5f, 0.0f, 0.0f, std::format("Y: {:8.2f}°", rot_euler_degrees.y).c_str()))
                {
                    glm::quat delta = glm::angleAxis(glm::radians(delta_y), glm::vec3(0, 1, 0));
                    #ifndef __INTELLISENSE__
                    selected_obj_ptr->SetRotation(rot_quat * delta);
                    #endif
                }
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_Z_AXYS);
                if (ImGui::DragFloat("##rotz", &delta_z, 0.5f, 0.0f, 0.0f, std::format("Z: {:8.2f}°", rot_euler_degrees.z).c_str()))
                {
                    glm::quat delta = glm::angleAxis(glm::radians(delta_z), glm::vec3(0, 0, 1));
                    #ifndef __INTELLISENSE__
                    selected_obj_ptr->SetRotation(rot_quat * delta);
                    #endif
                }
                ImGui::PopStyleColor();

                if (ImGui::Button("Set Identity Rot"))
                {
                    selected_obj_ptr->SetRotation(glm::identity<glm::quat>());
                }

                glm::vec3 scale = selected_obj_ptr->GetScale();
                
                if (selected_obj_ptr->m_physics_object 
                    && selected_obj_ptr->m_physics_object->GetShapeType() == CoreEngine::PhysicsShapeType::SPHERE)
                {
                    ImGui::SliderFloat("Scale##scaleslider1", &scale.x, 0.1f, MAX_OBJ_SCALE_FACTOR);
                    scale.y = scale.z = scale.x;
                }
                else 
                {
                    ImGui::SliderFloat3("Scale##scaleslider3", &scale.x, 0.1f, MAX_OBJ_SCALE_FACTOR);
                }
                if (ImGui::Button("Reset Scale"))
                {
                    scale = glm::vec3(1.0f);
                }
                
                if (scale != selected_obj_ptr->GetScale())
                {
                    selected_obj_ptr->SetScale(scale);
                }


                ImGui::Unindent();
            }  

            if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen) && selected_obj_ptr->m_render_model)
            {
                ImGui::Indent();
                const CoreEngine::Basic_Model* model = selected_obj_ptr->m_render_model.get();
                const CoreEngine::MathUtility::AABB aabb = model->GetWorldSpaceAABB();
                const auto min = aabb.min();
                const auto max = aabb.max();
                ImGui::Text("AABB: \nMIN[%f, %f, %f] \nMAX[%f, %f, %f]", min.x, min.y, min.z, max.x, max.y, max.z);
                ImGui::Checkbox("Highlight AABB", &m_selected_object_state.m_highlight_aabb_box);
                ImGui::Text("Meshes: %zu", model->GetMeshVectorConstReference().size());
                ImGui::TextUnformatted(("Scale: " + CoreEngine::CommonUtility::GlmVec3ToString(model->GetScale())).c_str());
                ImGui::Unindent();
            }

            ImGui::PushStyleColor(ImGuiCol_Button, COLOR_ORANGE);
            if (ImGui::Button("Unselect"))
            {
                OnSetSelectedSceneObject(CoreEngine::Scene3D::ObjectID::CreateInvalidID());
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            //MUST be last option to avoid segfault or a use after free
            ImGui::PushStyleColor(ImGuiCol_Button, COLOR_RED);
            if (ImGui::Button("Delete"))
            {
                CoreEngine::Scene3D::ObjectID obj = m_selected_object_state.m_object_id;
                OnSetSelectedSceneObject(CoreEngine::Scene3D::ObjectID::CreateInvalidID());
                m_scene.RemoveObject(obj);
            }
            ImGui::PopStyleColor();

            ImGui::Unindent();
        }
        ImGui::PopID();

        ImGui::End();
        
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);

    }

    void TrackViewerLayer::OnImGuiRender_BottomOptionPanel() noexcept
    {
        ImGuiIO& io = ImGui::GetIO();

        const float left_x_pixels      = std::floor(io.DisplaySize.x * m_left_panel_width_relative);
        const float panel_width_pixels = io.DisplaySize.x - left_x_pixels;

        const float default_height_px = io.DisplaySize.y * BOTTOM_PANEL_DEFAULT_HEIGHT_RELATIVE;
        const float min_height_px     = io.DisplaySize.y * BOTTOM_PANEL_MIN_HEIGHT_RELATIVE;
        const float max_height_px     = io.DisplaySize.y * BOTTOM_PANEL_MAX_HEIGHT_RELATIVE;

        const float bottom_y_pos      = io.DisplaySize.y - default_height_px;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, BORDER_SIZE_PIXELS);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        ImGui::PushStyleColor(ImGuiCol_Border, COLOR_BORDER);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, COLOR_BG);
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip,        COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,  COLOR_TRANSPARENT);
        ImGui::PushStyleColor(ImGuiCol_Text, COLOR_TEXT);

        ImGui::SetNextWindowPos(ImVec2(left_x_pixels, bottom_y_pos), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(panel_width_pixels, default_height_px), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(panel_width_pixels, min_height_px), ImVec2(panel_width_pixels, max_height_px));

        ImGui::Begin("Editor_3D_BottomOptionPanel", nullptr, window_flags);

        ImGui::SetWindowPos(ImVec2(left_x_pixels, io.DisplaySize.y - ImGui::GetWindowHeight()));
        ImGui::SetWindowSize(ImVec2(panel_width_pixels, ImGui::GetWindowHeight()));

        m_bottom_panel_height_relative = ImGui::GetWindowHeight() / io.DisplaySize.y;

        const auto selected_obj_ptr = m_scene.GetSceneObject(m_selected_object_state.m_object_id);
        if (selected_obj_ptr && selected_obj_ptr->m_render_model)
        {
            const auto DrawImage = [](GLuint id, ImVec2 size) -> void 
            {
                const ImVec2 tex_uv_0 (0, 0);
                const ImVec2 tex_uv_1 (1, 1);
                const ImVec4 tint_col (1, 1, 1, 1);
                ImGui::Image((ImTextureID)(uintptr_t)id, size, tex_uv_0, tex_uv_1, tint_col, COLOR_BORDER);
            };

            const auto DrawPlaceholderX = [](ImVec2 size) -> void 
            {
                ImGui::Dummy(size);
                ImVec2 left_top     (ImGui::GetItemRectMin());
                ImVec2 right_bottom (ImGui::GetItemRectMax());
                ImVec2 left_bottom  (left_top.x, right_bottom.y);
                ImVec2 right_top    (right_bottom.x, left_top.y);
                ImGui::GetWindowDrawList()->AddRect(left_top, right_bottom, IM_COL32(255, 0, 0, 255), 0.0f, ImDrawFlags{0}, BORDER_SIZE_PIXELS);
                ImGui::GetWindowDrawList()->AddLine(left_top, right_bottom, IM_COL32(255, 0, 0, 255));
                ImGui::GetWindowDrawList()->AddLine(left_bottom, right_top, IM_COL32(255, 0, 0, 255));
            };

            const ImVec2 area_available = ImGui::GetContentRegionAvail();
            const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;

            const int num_texture_types = 5;
            const float image_width = std::max(50.0f, (area_available.x * 0.8f - (num_texture_types - 1) * spacing.x) / num_texture_types);

            const ImVec2 image_size(image_width, image_width);

            const std::vector<CoreEngine::Mesh>& meshes = selected_obj_ptr->m_render_model->GetMeshVectorConstReference();

            const auto DrawOrPlaceholder = [&](std::shared_ptr<CoreEngine::Texture> tex, const char* tooltip_msg)
            {
                ImGui::SameLine();
                if (tex) DrawImage(tex->GetID(), image_size);
                else DrawPlaceholderX(image_size);
                if (ImGui::IsItemHovered()) 
                { 
                    ImGui::SetTooltip("%s", tooltip_msg); 
                }
            };

            for (size_t i = 0; i < meshes.size(); i++)
            {
                const std::shared_ptr<CoreEngine::MaterialPBR> material = meshes[i].GetMaterialConstSharedPtr();
                ENGINE_ASSERT(material && "At Editor_3D_Layer::OnImGuiRender_BottomOptionPanel(): Expected mesh material to not be nullptr.");

                ImGui::TextUnformatted(std::format("Mesh {:>3d}: ", i).c_str());

            #ifndef __INTELLISENSE__
                DrawOrPlaceholder(material->m_base_texture, "Base Texture");
                DrawOrPlaceholder(material->m_metallic_roughness_texture, "Metalic Roughness");
                DrawOrPlaceholder(material->m_normal_texture, "Normal Texture");
                DrawOrPlaceholder(material->m_occlusion_texture, "Occlusion Texture");
                DrawOrPlaceholder(material->m_emissive_texture, "Emissive Texture");
            #endif
            }

        }

        ImGui::End();

        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);
    }

    void TrackViewerLayer::OnSetRenderGUI(bool on) noexcept
    {
        m_render_gui = on;
        if (m_camera_controller->GetType() == CoreEngine::Basic_CameraController::Type::FreeCam)
        {
            reinterpret_cast<CoreEngine::FreeCam_CameraController*>(m_camera_controller.get())->SetOnlyLookAroundIfRightMouse(m_render_gui);
        }

        glfwSetInputMode(CoreEngine::Application::Get()->GetWindowPtr(m_handle)->GetGLFWwindow(), GLFW_CURSOR, m_render_gui ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        m_ignore_next_mouse_deltas = 5;
    }

    void TrackViewerLayer::OnShowNonTrianglemeshObjects(bool show) noexcept
    {
        m_show_non_triangle_meshes = show;

        const glm::vec3 offset = show ? glm::vec3(1'000'000.0f) : glm::vec3(-1'000'000.0f);

        std::vector<CoreEngine::Scene3D::Slot>& slots = m_scene.GetSlotVectorRef();

        for (CoreEngine::Scene3D::Slot& slot : slots)
        {
            auto* obj = slot.m_object.get();

            if (obj && obj->m_render_model && obj->m_render_model->GetModelType() != CoreEngine::Basic_Model::ModelType::POINTS_MODEL)
            {
                obj->SetPosition(obj->GetPosition() + offset);
            }
        }
    }

    void TrackViewerLayer::CreateInstance() noexcept
    {
        ENGINE_ASSERT( ! s_instance && "There should only ever be one TasInputLayer active at one time.");

        using Cdis = CoreEngine::Window::WindowCreationConfig::CallbackDisableFlags;

        constexpr CoreEngine::Window::WindowCreationConfig config 
        {
            .m_title                       = "TrackView   ",
            .m_relative_size               = {1.0f, 0.95f},
            .m_callback_disable_flags      = {},
            .m_imgui_flags                 = {},
            .m_MSAA_sample_count           = 8,
            .m_is_decorated                = true,
            .m_has_transparent_framebuffer = false,
            .m_is_clickthrough             = false
        };

        CoreEngine::Application::Get()->QueueCreateWindowAndPushLayer<TrackViewerLayer>(config);
    }

    bool TrackViewerLayer::InstanceExists() noexcept
    {
        return s_instance != nullptr;
    }

    void TrackViewerLayer::DeleteInstance() noexcept
    {
        if (! InstanceExists()) return;
        CoreEngine::Application::Get()->QueueDeleteWindowLayerStack(s_instance->m_handle);
    }
}
