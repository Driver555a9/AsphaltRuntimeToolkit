#pragma once

#define NOMINMAX
#include <cstdint>
#include <vector>

#define HAS_GET_MAIN_MODULE_FUNCTION
#include "BulletTypes.h"

[[nodiscard]] uintptr_t GetMainGameModule() noexcept;

namespace AsphaltDLL
{
    namespace DetourFunctions 
    {
        enum class HookState
        {
            NotInPlace, InPlaceDisabled, InPlaceEnabled
        };

        // Internal, however OnHandleGeneralInBuffer may be called by external threads
        // Must LOCK_CURRENT_STATE_MUTEX() before call
        namespace StateManager
        {
            void OnHandleGeneralInBuffer() noexcept;
        }

        ////////////////////////////////////////////
        // This function called from the games main thread
        // Dispatches logic for new frame (including Physics worker thread)
        // On this same thread runs camera and cp logic
        // May not be used for replay new tick entry point, because this function runs even in menus
        ////////////////////////////////////////////
        namespace NewLogicTickDispatcher
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////////////////////
        // Function called on each new logical frame. 
        // R8 = elapsed delta time in microseconds
        // Handles both game side logic (nitro, drift etc.) and calls Physics Update function
        // Does not run whilst in pause menu for whatever reason
        ////////////////////////////////////////////////////////////
        namespace PhysicsContextNewFrame 
        {
            void QueueSkipSubsequentTicks(uint32_t amount) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////////////////////
        // [rdx] is float seconds interval time from last call
        // Function internally increments a counter with [rdx] and does as many physics ticks until that counter is < 0, subtracting PF each tick
        // [rcx] is a proxy between game world and discretedynamicsworld
        ////////////////////////////////////////////////////////////
        namespace PhysicsWorldWrapperNewTick
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////////////////////
        // Bullet spec DiscreteDynamicsWorld::InternalSingleStepSimulation on the actual physics world object
        ////////////////////////////////////////////////////////////
        namespace InternalSingleStepSimulation
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////////////////////
        // DiscreteDynamicsWorldDestructor
        ////////////////////////////////////////////////////////////
        namespace DiscreteDynamicsWorldDestructor
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////////////////////
        // Processes collision pairs - hook because our custom rigidbodies may have no valid m_user_object_ptr
        ////////////////////////////////////////////////////////////
        namespace ProcessCollisionPolicyGetShouldCollide
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        //////////////////////////////////////////////////////////
        // PhysicsContext MERSENNE TWISTER PRNG - used for Barrel Rolls, must be reset per race!
        //////////////////////////////////////////////////////////
        namespace BarrelPRNG
        {
            void Reset() noexcept;
            float NextFloat01() noexcept;
            double NextDouble01() noexcept;
        }

        namespace BarrelRandomLerp
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        namespace BarrelRandomBool
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Function writes final steer input normalized between [-1;+1]
        ///////////////////////////////////////
        namespace SteeringValue
        {
            bool SpoofCallToSteerValueFunction(float steer) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Function writes final brake value [-1 brake; +1 none]
        // First gameplay function we hook, therefore it initiates the new frame
        ///////////////////////////////////////
        namespace BrakeValue
        {
            bool SpoofCallToBrakeValueFunction(float brake) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Huge functions that does lots of shit, including setting accelerator
        // Accelerator at RCX + 0x1D0
        ///////////////////////////////////////
        namespace AcceleratorValue
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Function is called when any nitro button is pressed
        // RCX parameter = NitroState; if provided rcx we can spoof a call to enable nitro without relying on input
        ///////////////////////////////////////
        namespace EnableNitro 
        {
            // Nitro func won't run unless called, therefore it can not manage its own state each time (New frame func will manage it)
            [[nodiscard]] uint8_t GetAndResetNitroFunctionWasCalledCounter() noexcept;
            void SpoofCallToEnableNitroFunction(std::uint32_t count) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Decrease of nitro goes through here
        // Writes encrypted 12 bytes [RCX+0x18C]
        // Function also does other shit but who cares
        ///////////////////////////////////////
        namespace DecreaseNitroBarFunc
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }
        
        ///////////////////////////////////////
        // Increase of nitro goes through here
        // Writes encrypted 12 bytes [RCX+0x18C]
        // Function also does other shit but who cares
        ///////////////////////////////////////
        namespace IncreaseNitroBarFunc
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Utility that enables simple access to nitro bar state
        // No function detour
        ///////////////////////////////////////
        namespace UpdateNitroBar 
        {
            bool WriteNitroBar(float value) noexcept;
            [[nodiscard]] float ReadNitroBar() noexcept;
        }

        ///////////////////////////////////////
        // Function updates transform of car
        // [rcx] = local racer base pointer
        ///////////////////////////////////////
        namespace LocalRacerAccessPoint
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Function useful for deducing local racer struct;
        // LocalRacerAccessPoint() may be called with multiple dynamic objects
        ///////////////////////////////////////
        namespace GetLocalRacerStruct
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Calls Set position (rax+80)
        // Calls Set rotation (rax+88)
        // Sets fov rad (movss [rcx+00000128],xmm0)
        ///////////////////////////////////////
        namespace CameraUpdate
        {
            void PatchDisableGameFovWriteInstruction() noexcept;
            void PatchEnableGameFovWriteInstruction() noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Useful to easily enable / disable respawn or wreck functionality
        ///////////////////////////////////////
        namespace NativeQueueRacerRespawn
        {
            [[nodiscard]] bool GetRespawnIsAllowed() noexcept;
            void SetRespawnIsAllowed(bool on) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        namespace AllowWreck
        {
            [[nodiscard]] bool GetWreckIsAllowed() noexcept;
            void SetWreckIsAllowed(bool on) noexcept;
        }

        ///////////////////////////////////////
        // Indeterministic functions that stabilize the car while in barrel roll
        // Likely caused by garbage initialized object passed into function
        // Only called whilst car is doing a barrel roll, only called once per PF tick
        // Functions write to angular velocity
        ///////////////////////////////////////
        namespace BarrelRollStabilization
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        namespace BarrelYawStabilization
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Indeterministic function that writes to racer transform
        ///////////////////////////////////////
        namespace FinalRacerTransformWriter
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Called on dynamic object wreck e.g. taffic or racer
        // Deploys breakables specifically (not main wreck entrypoint)
        ///////////////////////////////////////
        namespace OnWreckDeployBreakables
        {
            std::uint64_t GetMonotonicWreckSessionCount() noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Switch case for different inputs; notably for respawn button press
        ///////////////////////////////////////
        namespace OnRespawnButtonPressed
        {
            void SpoofCallToRespawnInputFunc() noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Getter that returns 1/PF - Physics cycle interval as float secs
        ///////////////////////////////////////
        namespace GetPhysicsInterval
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Begin Race function
        // Arbitrary function called when beginning a race, to detect "is in race" state
        ///////////////////////////////////////
        namespace OnBeginRaceFunction
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Arbitrary function called when clicking play or restart
        ///////////////////////////////////////
        namespace OnClickPlayFunction
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // End Race function
        // Arbitrary function called when ending a race, to detect "is in race" state
        // Called precisely at Fin line if race isn't aborted
        ///////////////////////////////////////
        namespace OnEndRaceFunction
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Writes race % value we can read
        ///////////////////////////////////////
        namespace OnUpdateRaceProgress
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Writes CP value we can read
        ///////////////////////////////////////
        namespace OnUpdateCheckpoint
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////
        // Handles racer track location logic
        ///////////////////////////////////////
        namespace SegmentResolve
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////
        // Sets float value obfuscated as 3 * 4 bytes into the destination directly
        // Destination must be final game storage address
        // Mind that this may only be called on the same thread as where the value is read again
        ////////////////////////////////////////////
        namespace FloatXorObfuscationSetter
        {
            void EncryptToAddress(void* p_dest, float value) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////
        // Reads float value obfuscated as 3 * 4 bytes and returns it
        // Mind that this may only be called on the same thread as where the value was written
        ////////////////////////////////////////////
        namespace FloatXorObfuscationGetter
        {
            [[nodiscard]] float DecryptFromAddress(void* original_addr) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }
        
        ////////////////////////////////////////////
        // Hooks for restarting race (slow path)
        ////////////////////////////////////////////
        namespace PauseMenuLogic
        {
            void QueueRestart() noexcept;
            void QueueQuit() noexcept;
            void QueueNothing() noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////
        // Query function that checks if track should be reset and if so calls ProcessLevelResetFadePhase
        ////////////////////////////////////////////
        namespace WorldShouldResetQuery
        {
            void QueueResetWorld() noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////
        // Initiates end of race track reset for incomming replay
        ////////////////////////////////////////////
        namespace ProcessLevelResetFadePhase
        {
            void SpoofCallToProcessLevelResetFadePhase(uintptr_t a1, uintptr_t* a2) noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ////////////////////////////////////////////
        // Function returns 0 or 1 to render or hide all of the GUI
        ////////////////////////////////////////////
        namespace RenderGUIToggle
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////////
        // Animation progress function, useful for skipping animations
        ///////////////////////////////////////////
        namespace AnimationProgressFunction
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////////
        // In actuality one of the first functions starting new frame
        // DT here can be changed and seems to only affect UI animation
        ///////////////////////////////////////////
        namespace SpeedUpUIAnimations
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////////
        // Updates visibility of cursor
        ///////////////////////////////////////////
        namespace UpdateCursorVisibility
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        ///////////////////////////////////////////
        // Getter to look if paused (no logic update)
        ///////////////////////////////////////////
        namespace IsPaused
        {
            [[nodiscard]] bool GetIsPaused() noexcept;
        }


        ////////////////////////////////////////////
        // XInputGetState() gives controller state
        ////////////////////////////////////////////
        namespace XInput_GetState
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        /////////////////////////////////////////
        // UcrtBaseRand
        // E.g. used for wreck physics / wreck camera
        /////////////////////////////////////////
        namespace UcrtBaseRand
        {
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        namespace BVHBroadphaseTraversal
        {
            struct DumpedNode 
            {
                BulletTypes::BroadphaseProxy*  m_broadphase_proxy {};
                BulletTypes::Vector3           m_aabb_min {};
                BulletTypes::Vector3           m_aabb_max {};
                bool                           m_is_from_static_tree {};
            };

            [[nodiscard]] std::vector<DumpedNode> DumpAllLeaves() noexcept;
            bool SetupHook() noexcept;
            bool RemoveHook() noexcept;
            bool EnableHook() noexcept;
            bool DisableHook() noexcept;
            [[nodiscard]] HookState GetHookState() noexcept;
        }

        namespace Experimental 
        {
            namespace InitiateNewFrame
            {
                bool SetupHook() noexcept;
                bool RemoveHook() noexcept;
                bool EnableHook() noexcept;
                bool DisableHook() noexcept;
                [[nodiscard]] HookState GetHookState() noexcept;
            }

            namespace NewFrameSubscriberList
            {
                bool SetupHook() noexcept;
                bool RemoveHook() noexcept;
                bool EnableHook() noexcept;
                bool DisableHook() noexcept;
                [[nodiscard]] HookState GetHookState() noexcept;
            }

            namespace MainFpsLimiter
            {
                bool SetupHook() noexcept;
                bool RemoveHook() noexcept;
                bool EnableHook() noexcept;
                bool DisableHook() noexcept;
                [[nodiscard]] HookState GetHookState() noexcept;
            }

            namespace QueryPerformanceCounterHook
            {
                void AdvanceVirtualTime(int64_t micros) noexcept;
                void SyncToRealTime() noexcept;
                int64_t GetRealTimeMicros() noexcept;
                void Initialize() noexcept;
                bool SetupHook() noexcept;
                bool RemoveHook() noexcept;
                bool EnableHook() noexcept;
                bool DisableHook() noexcept;
                [[nodiscard]] HookState GetHookState() noexcept;
            }

            namespace OnRaycastVehicleUpdate
            {
                bool SetupHook() noexcept;
                bool RemoveHook() noexcept;
                bool EnableHook() noexcept;
                bool DisableHook() noexcept;
                [[nodiscard]] HookState GetHookState() noexcept;
            }

            namespace PhysicsWorldRaycast
            {
                [[nodiscard]] BulletTypes::RaycastOutput SpoofCallToCastRay(BulletTypes::Vector3 start, BulletTypes::Vector3 end, uint16_t layer_mask, uint16_t query_flags) noexcept;
                bool SetupHook() noexcept;
                bool RemoveHook() noexcept;
                bool EnableHook() noexcept;
                bool DisableHook() noexcept;
                [[nodiscard]] HookState GetHookState() noexcept;
            }
        }
    }
}
