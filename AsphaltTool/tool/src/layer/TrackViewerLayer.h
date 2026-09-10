#include "common/Replay.h"
#include "core/layer/Editor_3D_Layer.h"
#include "core/rendering/DrawLines3D_RenderPipeline.h"
#include <cstddef>
#include <cstdint>

namespace AsphaltTas
{
    class TrackViewerLayer : public CoreEngine::Editor_3D_Layer
    {
    public:
        explicit TrackViewerLayer(CoreEngine::Window::Handle handle) noexcept;
        virtual ~TrackViewerLayer() noexcept;    

        virtual void OnEvent(CoreEngine::Basic_Event& e) noexcept override;
        virtual void OnUpdate(CoreEngine::Units::MicroSecond dt) noexcept override;
        virtual void OnRender() noexcept override;
        virtual void OnImGuiRender() noexcept override; 

        void OnImGuiRender_LeftOptionPanel()   noexcept;
        void OnImGuiRender_BottomOptionPanel() noexcept;
        void OnImGuiRender_RightOptionPanel()  noexcept;

        void LoadTrackFromFile(const std::filesystem::path& path) noexcept;
        void LoadColorDefFromFile(const std::filesystem::path& path) noexcept;
        void LoadReplay(std::optional<Replay> replay) noexcept;

        static void CreateInstance() noexcept;
        [[nodiscard]] static bool InstanceExists() noexcept;
        static void DeleteInstance() noexcept;
        
    private:
        std::optional<AsphaltTas::Replay> m_current_replay;
        CoreEngine::DrawLines3D_RenderPipeline m_replay_draw_line_pipeline;
        std::filesystem::path m_next_track_path = "";
        uint32_t m_ignore_next_mouse_deltas = 0;
        bool m_has_to_move_track_file = false; 
        bool m_render_gui = true;
        bool m_show_non_triangle_meshes = false; // This is a hack that just moves the other objects

        constexpr static char COLOR_DEF_FILE_NAME[] = "trackview.COLORDEF";
        constexpr static size_t COLOR_DEFS_INDEX_RAMPS     = 0;
        constexpr static size_t COLOR_DEFS_INDEX_DYNAMICS  = 1;
        constexpr static size_t COLOR_DEFS_INDEX_BOX       = 2;
        constexpr static size_t COLOR_DEFS_INDEX_SPHERE    = 3;
        constexpr static size_t COLOR_DEFS_INDEX_CYLINDER  = 4;
        constexpr static size_t COLOR_DEFS_INDEX_CAPSULE   = 5;
        constexpr static size_t COLOR_DEFS_BEGIN_MATERIALS = 6;
        std::vector<glm::vec3> m_color_defs = 
        {
            {1.0f, 0.1568f, 0.0f},        //RAMPS
            {1.0f, 0.0f, 0.39215f},       //DYNAMICS
            {0.55f, 0.65f, 0.90f},        //BOX
            {0.70f, 0.78f, 0.95f},        //Sphere
            {0.850f, 1.000f, 0.450f},     //Cylinder
            {0.350f, 0.700f, 0.450f},     //Capsule
            {0.2354f, 1.000f, 0.0f},      
            {0.000f, 0.650f, 1.000f},
            {1.000f, 0.850f, 0.000f},
            {0.600f, 0.250f, 1.000f},
            {0.000f, 0.900f, 0.900f},
            {1.000f, 0.450f, 0.000f},
            {0.800f, 1.000f, 0.000f},
            {0.000f, 0.350f, 1.000f},
            {1.000f, 1.000f, 0.000f},
            {0.450f, 0.000f, 1.000f},
            {0.000f, 1.000f, 0.650f},
            {0.850f, 0.550f, 0.000f},
            {0.300f, 0.750f, 1.000f},
            {1.000f, 0.800f, 0.350f},
            {0.700f, 0.000f, 0.850f},
            {0.450f, 1.000f, 0.850f},
            {0.900f, 0.900f, 0.550f},
            {0.200f, 0.500f, 1.000f},
            {0.750f, 0.300f, 0.900f},
            {0.100f, 0.850f, 0.500f},
            {0.950f, 0.650f, 0.150f},
            {0.150f, 0.850f, 1.000f},
            {0.550f, 0.000f, 0.700f}
        };

        void OnSetRenderGUI(bool on) noexcept;
        void OnShowNonTrianglemeshObjects(bool show) noexcept;

        // Prevent editor layer from selecting or moving objects
        [[nodiscard]] virtual bool OnMousePressed(CoreEngine::MousePressedEvent& e)   noexcept override 
        { 
            return CoreEngine::Editor_3D_Layer::OnMousePressed(e); 
        }
        [[nodiscard]] virtual bool OnMouseMoved(CoreEngine::MouseMovedEvent& e)       noexcept override 
        { 
            if ( m_ignore_next_mouse_deltas > 0) 
            {
                m_ignore_next_mouse_deltas--;
                m_input_state.m_mouse_move_delta = {0, 0};
                m_input_state.m_previous_mouse_pos = {e.GetX(), e.GetY()};
                return false;
            }
            return CoreEngine::Freecam_3D_Layer::OnMouseMoved(e); 
        }
        [[nodiscard]] virtual bool OnMouseScrolled(CoreEngine::MouseScrolledEvent& e) noexcept override 
        { 
            return CoreEngine::Freecam_3D_Layer::OnMouseScrolled(e);  
        }

        static inline TrackViewerLayer* s_instance = nullptr;
    };

};