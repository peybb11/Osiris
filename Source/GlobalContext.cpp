#include "GlobalContext.h"

#if IS_WIN32()
#include <imgui/imgui_impl_dx9.h>
#include <imgui/imgui_impl_win32.h>
#else
#include <imgui/imgui_impl_sdl.h>
#include <imgui/imgui_impl_opengl3.h>
#endif

#include "EventListener.h"
#include "GameData.h"
#include "GUI.h"
#include "Hooks.h"
#include "InventoryChanger/InventoryChanger.h"
#include "Memory.h"
#include "Hacks/Aimbot.h"
#include "Hacks/AntiAim.h"
#include "Hacks/Backtrack.h"
#include "Hacks/Chams.h"
#include "Hacks/EnginePrediction.h"
#include "Hacks/Glow.h"
#include "Hacks/Misc.h"
#include "Hacks/Sound.h"
#include "Hacks/StreamProofESP.h"
#include "Hacks/Triggerbot.h"
#include "Hacks/Visuals.h"
#include "SDK/ClientClass.h"
#include "SDK/Constants/ClassId.h"
#include "SDK/Constants/FrameStage.h"
#include "SDK/Constants/UserMessages.h"
#include "SDK/Engine.h"
#include "SDK/Entity.h"
#include "SDK/EntityList.h"
#include "SDK/GlobalVars.h"
#include "SDK/InputSystem.h"
#include "SDK/LocalPlayer.h"
#include "SDK/ModelRender.h"
#include "SDK/Recv.h"
#include "SDK/SoundEmitter.h"
#include "SDK/SoundInfo.h"
#include "SDK/StudioRender.h"
#include "SDK/Surface.h"
#include "SDK/UserCmd.h"
#include "SDK/ViewSetup.h"

#include "Interfaces.h"

bool GlobalContext::createMoveHook(float inputSampleTime, UserCmd* cmd)
{
    auto result = hooks->clientMode.callOriginal<bool, WIN32_LINUX(24, 25)>(inputSampleTime, cmd);

    if (!cmd->commandNumber)
        return result;

#if IS_WIN32()
    // bool& sendPacket = *reinterpret_cast<bool*>(*reinterpret_cast<std::uintptr_t*>(FRAME_ADDRESS()) - 0x1C);
    // since 19.02.2022 game update sendPacket is no longer on stack
    bool sendPacket = true;
#else
    bool dummy;
    bool& sendPacket = dummy;
#endif
    static auto previousViewAngles{ cmd->viewangles };
    const auto currentViewAngles{ cmd->viewangles };

    memory->globalVars->serverTime(cmd);
    Misc::nadePredict(*interfaces);
    Misc::antiAfkKick(cmd);
    Misc::fastStop(cmd);
    Misc::prepareRevolver(engineInterfaces->getEngine(), *memory, cmd);
    visuals->removeShadows();
    Misc::runReportbot(engineInterfaces->getEngine(), *clientInterfaces, *interfaces, *memory);
    Misc::bunnyHop(cmd);
    Misc::autoStrafe(cmd);
    Misc::removeCrouchCooldown(cmd);
    Misc::autoPistol(*memory, cmd);
    Misc::autoReload(cmd);
    Misc::updateClanTag(*memory);
    Misc::fakeBan(engineInterfaces->getEngine(), *interfaces, *memory);
    Misc::stealNames(engineInterfaces->getEngine(), *clientInterfaces, *interfaces, *memory);
    Misc::revealRanks(*clientInterfaces, cmd);
    Misc::quickReload(*clientInterfaces, *interfaces, cmd);
    Misc::fixTabletSignal();
    Misc::slowwalk(cmd);

    EnginePrediction::run(*clientInterfaces, *memory, cmd);

    Aimbot::run(*engineInterfaces, *clientInterfaces, *interfaces, *config, *memory, cmd);
    Triggerbot::run(engineInterfaces->engineTrace, *interfaces, *memory, *config, cmd);
    Backtrack::run(*clientInterfaces, *engineInterfaces, *interfaces, *memory, cmd);
    Misc::edgejump(cmd);
    Misc::moonwalk(cmd);
    Misc::fastPlant(engineInterfaces->engineTrace, *interfaces, cmd);

    if (!(cmd->buttons & (UserCmd::IN_ATTACK | UserCmd::IN_ATTACK2))) {
        Misc::chokePackets(engineInterfaces->getEngine(), sendPacket);
        AntiAim::run(cmd, previousViewAngles, currentViewAngles, sendPacket);
    }

    auto viewAnglesDelta{ cmd->viewangles - previousViewAngles };
    viewAnglesDelta.normalize();
    viewAnglesDelta.x = std::clamp(viewAnglesDelta.x, -Misc::maxAngleDelta(), Misc::maxAngleDelta());
    viewAnglesDelta.y = std::clamp(viewAnglesDelta.y, -Misc::maxAngleDelta(), Misc::maxAngleDelta());

    cmd->viewangles = previousViewAngles + viewAnglesDelta;

    cmd->viewangles.normalize();
    Misc::fixMovement(cmd, currentViewAngles.y);

    cmd->viewangles.x = std::clamp(cmd->viewangles.x, -89.0f, 89.0f);
    cmd->viewangles.y = std::clamp(cmd->viewangles.y, -180.0f, 180.0f);
    cmd->viewangles.z = 0.0f;
    cmd->forwardmove = std::clamp(cmd->forwardmove, -450.0f, 450.0f);
    cmd->sidemove = std::clamp(cmd->sidemove, -450.0f, 450.0f);

    previousViewAngles = cmd->viewangles;

    return false;
}

void GlobalContext::doPostScreenEffectsHook(void* param)
{
    if (engineInterfaces->getEngine().isInGame()) {
        visuals->thirdperson();
        visuals->inverseRagdollGravity();
        visuals->reduceFlashEffect();
        visuals->updateBrightness();
        visuals->remove3dSky();
        Glow::render(*engineInterfaces, *clientInterfaces, *interfaces, *memory);
    }
    hooks->clientMode.callOriginal<void, WIN32_LINUX(44, 45)>(param);
}

float GlobalContext::getViewModelFovHook()
{
    float additionalFov = visuals->viewModelFov();
    if (localPlayer) {
        if (const auto activeWeapon = Entity::from(retSpoofGadgets.client, localPlayer.get().getActiveWeapon()); activeWeapon.getPOD() != nullptr && activeWeapon.getNetworkable().getClientClass()->classId == ClassId::Tablet)
            additionalFov = 0.0f;
    }

    return hooks->clientMode.callOriginal<float, WIN32_LINUX(35, 36)>() + additionalFov;
}

void GlobalContext::drawModelExecuteHook(void* ctx, void* state, const ModelRenderInfo& info, matrix3x4* customBoneToWorld)
{
    if (interfaces->getStudioRender().isForcedMaterialOverride())
        return hooks->modelRender.callOriginal<void, 21>(ctx, state, std::cref(info), customBoneToWorld);

    if (visuals->removeHands(info.model->name) || visuals->removeSleeves(info.model->name) || visuals->removeWeapons(info.model->name))
        return;

    if (static Chams chams; !chams.render(engineInterfaces->getEngine(), *clientInterfaces, *interfaces, *memory, *config, ctx, state, info, customBoneToWorld))
        hooks->modelRender.callOriginal<void, 21>(ctx, state, std::cref(info), customBoneToWorld);

    interfaces->getStudioRender().forcedMaterialOverride(nullptr);
}

bool GlobalContext::svCheatsGetBoolHook(void* _this, std::uintptr_t returnAddress)
{
    if (returnAddress == memory->cameraThink && visuals->isThirdpersonOn())
        return true;

    return hooks->svCheats.getOriginal<bool, WIN32_LINUX(13, 16)>()(_this);
}

void GlobalContext::frameStageNotifyHook(csgo::FrameStage stage)
{
    [[maybe_unused]] static auto backtrackInit = (Backtrack::init(*interfaces), false);
    if (engineInterfaces->getEngine().isConnected() && !engineInterfaces->getEngine().isInGame())
        Misc::changeName(engineInterfaces->getEngine(), *interfaces, *memory, true, nullptr, 0.0f);

    if (stage == csgo::FrameStage::START)
        GameData::update(*clientInterfaces, *engineInterfaces, *interfaces, *memory);

    if (stage == csgo::FrameStage::RENDER_START) {
        Misc::preserveKillfeed(*memory);
        Misc::disablePanoramablur(*interfaces);
        visuals->colorWorld();
        Misc::updateEventListeners(*engineInterfaces);
        visuals->updateEventListeners();
    }
    if (engineInterfaces->getEngine().isInGame()) {
        visuals->skybox(stage);
        visuals->removeBlur(stage);
        Misc::oppositeHandKnife(*interfaces, stage);
        visuals->removeGrass(stage);
        visuals->modifySmoke(stage);
        visuals->disablePostProcessing(stage);
        visuals->removeVisualRecoil(stage);
        visuals->applyZoom(stage);
        Misc::fixAnimationLOD(engineInterfaces->getEngine(), *clientInterfaces, *memory, stage);
        Backtrack::update(*engineInterfaces, *clientInterfaces, *interfaces, *memory, stage);
    }
    inventory_changer::InventoryChanger::instance(*interfaces, *memory).run(*engineInterfaces, *clientInterfaces, *interfaces, *memory, stage);

    hooks->client.callOriginal<void, 37>(stage);
}

int GlobalContext::emitSoundHook(void* filter, int entityIndex, int channel, const char* soundEntry, unsigned int soundEntryHash, const char* sample, float volume, int seed, int soundLevel, int flags, int pitch, const Vector& origin, const Vector& direction, void* utlVecOrigins, bool updatePositions, float soundtime, int speakerentity, void* soundParams)
{
    Sound::modulateSound(*clientInterfaces, *memory, soundEntry, entityIndex, volume);
    Misc::autoAccept(*interfaces, *memory, soundEntry);

    volume = std::clamp(volume, 0.0f, 1.0f);
    return hooks->sound.callOriginal<int, WIN32_LINUX(5, 6)>(filter, entityIndex, channel, soundEntry, soundEntryHash, sample, volume, seed, soundLevel, flags, pitch, std::cref(origin), std::cref(direction), utlVecOrigins, updatePositions, soundtime, speakerentity, soundParams);
}

bool GlobalContext::shouldDrawFogHook(std::uintptr_t returnAddress)
{
#if IS_WIN32()
    if constexpr (std::is_same_v<HookType, MinHook>) {
        if (returnAddress != memory->shouldDrawFogReturnAddress)
            return hooks->clientMode.callOriginal<bool, 17>();
    }
#endif

    return !visuals->shouldRemoveFog();
}

bool GlobalContext::shouldDrawViewModelHook()
{
    if (visuals->isZoomOn() && localPlayer && localPlayer.get().fov() < 45 && localPlayer.get().fovStart() < 45)
        return false;
    return hooks->clientMode.callOriginal<bool, WIN32_LINUX(27, 28)>();
}

void GlobalContext::lockCursorHook()
{
    if (gui->isOpen())
        return interfaces->getSurface().unlockCursor();
    return hooks->surface.callOriginal<void, 67>();
}

void GlobalContext::setDrawColorHook(int r, int g, int b, int a, std::uintptr_t returnAddress)
{
    if (visuals->shouldRemoveScopeOverlay() && (returnAddress == memory->scopeDust || returnAddress == memory->scopeArc))
        a = 0;
    hooks->surface.callOriginal<void, WIN32_LINUX(15, 14)>(r, g, b, a);
}

void GlobalContext::overrideViewHook(ViewSetup* setup)
{
    if (localPlayer && !localPlayer.get().isScoped())
        setup->fov += visuals->fov();
    setup->farZ += visuals->farZ() * 10;
    hooks->clientMode.callOriginal<void, WIN32_LINUX(18, 19)>(setup);
}

int GlobalContext::dispatchSoundHook(SoundInfo& soundInfo)
{
    if (const char* soundName = interfaces->getSoundEmitter().getSoundName(soundInfo.soundIndex)) {
        Sound::modulateSound(*clientInterfaces, *memory, soundName, soundInfo.entityIndex, soundInfo.volume);
        soundInfo.volume = std::clamp(soundInfo.volume, 0.0f, 1.0f);
    }
    return hooks->originalDispatchSound(soundInfo);
}

void GlobalContext::render2dEffectsPreHudHook(void* viewSetup)
{
    visuals->applyScreenEffects();
    visuals->hitEffect();
    hooks->viewRender.callOriginal<void, WIN32_LINUX(39, 40)>(viewSetup);
}

const DemoPlaybackParameters* GlobalContext::getDemoPlaybackParametersHook(std::uintptr_t returnAddress)
{
    const auto params = hooks->engine.callOriginal<const DemoPlaybackParameters*, WIN32_LINUX(218, 219)>();

    if (params && Misc::shouldRevealSuspect() && returnAddress != memory->demoFileEndReached) {
        static DemoPlaybackParameters customParams;
        customParams = *params;
        customParams.anonymousPlayerIdentity = false;
        return &customParams;
    }

    return params;
}

bool GlobalContext::dispatchUserMessageHook(csgo::UserMessageType type, int passthroughFlags, int size, const void* data)
{
    if (type == csgo::UserMessageType::Text)
        inventory_changer::InventoryChanger::instance(*interfaces, *memory).onUserTextMsg(*memory, data, size);
    else if (type == csgo::UserMessageType::VoteStart)
        Misc::onVoteStart(*globalContext->clientInterfaces, *interfaces, *memory, data, size);
    else if (type == csgo::UserMessageType::VotePass)
        Misc::onVotePass(*memory);
    else if (type == csgo::UserMessageType::VoteFailed)
        Misc::onVoteFailed(*memory);

    return hooks->client.callOriginal<bool, 38>(type, passthroughFlags, size, data);
}

bool GlobalContext::isPlayingDemoHook(std::uintptr_t returnAddress, std::uintptr_t frameAddress)
{
    if (Misc::shouldRevealMoney() && returnAddress == memory->demoOrHLTV && *reinterpret_cast<std::uintptr_t*>(frameAddress + WIN32_LINUX(8, 24)) == memory->money)
        return true;

    return hooks->engine.callOriginal<bool, 82>();
}

void GlobalContext::updateColorCorrectionWeightsHook()
{
    hooks->clientMode.callOriginal<void, WIN32_LINUX(58, 61)>();

    globalContext->visuals->performColorCorrection();
    if (globalContext->visuals->shouldRemoveScopeOverlay())
        *memory->vignette = 0.0f;
}

float GlobalContext::getScreenAspectRatioHook(int width, int height)
{
    if (Misc::aspectRatio() != 0.0f)
        return Misc::aspectRatio();
    return hooks->engine.callOriginal<float, 101>(width, height);
}

void GlobalContext::renderSmokeOverlayHook(bool update)
{
    if (visuals->shouldRemoveSmoke() || visuals->isSmokeWireframe())
        *reinterpret_cast<float*>(std::uintptr_t(memory->viewRender) + WIN32_LINUX(0x588, 0x648)) = 0.0f;
    else
        hooks->viewRender.callOriginal<void, WIN32_LINUX(41, 42)>(update);
}

double GlobalContext::getArgAsNumberHook(void* params, int index, std::uintptr_t returnAddress)
{
    const auto result = hooks->panoramaMarshallHelper.callOriginal<double, 5>(params, index);
    inventory_changer::InventoryChanger::instance(*interfaces, *memory).getArgAsNumberHook(memory->inventoryChangerReturnAddresses, static_cast<int>(result), returnAddress);
    return result;
}

const char* GlobalContext::getArgAsStringHook(void* params, int index, std::uintptr_t returnAddress)
{
    const auto result = hooks->panoramaMarshallHelper.callOriginal<const char*, 7>(params, index);

    if (result)
        inventory_changer::InventoryChanger::instance(*interfaces, *memory).getArgAsStringHook(memory->inventoryChangerReturnAddresses, *memory, result, returnAddress, params);

    return result;
}

void GlobalContext::setResultIntHook(void* params, int result, std::uintptr_t returnAddress)
{
    result = inventory_changer::InventoryChanger::instance(*interfaces, *memory).setResultIntHook(memory->inventoryChangerReturnAddresses, returnAddress, params, result);
    hooks->panoramaMarshallHelper.callOriginal<void, WIN32_LINUX(14, 11)>(params, result);
}

unsigned GlobalContext::getNumArgsHook(void* params, std::uintptr_t returnAddress)
{
    const auto result = hooks->panoramaMarshallHelper.callOriginal<unsigned, 1>(params);
    inventory_changer::InventoryChanger::instance(*interfaces, *memory).getNumArgsHook(memory->inventoryChangerReturnAddresses, result, returnAddress, params);
    return result;
}

void GlobalContext::updateInventoryEquippedStateHook(std::uintptr_t inventory, csgo::ItemId itemID, csgo::Team team, int slot, bool swap)
{
    inventory_changer::InventoryChanger::instance(*interfaces, *memory).onItemEquip(team, slot, itemID);
    hooks->inventoryManager.callOriginal<void, WIN32_LINUX(29, 30)>(inventory, itemID, team, slot, swap);
}

void GlobalContext::soUpdatedHook(SOID owner, csgo::pod::SharedObject* object, int event)
{
    inventory_changer::InventoryChanger::instance(*interfaces, *memory).onSoUpdated(SharedObject::from(retSpoofGadgets.client, object));
    hooks->inventory.callOriginal<void, 1>(owner, object, event);
}

#if IS_WIN32()
LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void* GlobalContext::allocKeyValuesMemoryHook(int size, std::uintptr_t returnAddress)
{
    if (returnAddress == memory->keyValuesAllocEngine || returnAddress == memory->keyValuesAllocClient)
        return nullptr;
    return hooks->keyValuesSystem.callOriginal<void*, 2>(size);
}

LRESULT GlobalContext::wndProcHook(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (state == GlobalContext::State::Initialized) {
        ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam);
        interfaces->getInputSystem().enableInput(!gui->isOpen());
    } else if (state == GlobalContext::State::NotInitialized) {
        state = GlobalContext::State::Initializing;

        const windows_platform::DynamicLibrary clientDLL{ windows_platform::DynamicLibraryWrapper{}, CLIENT_DLL };
        clientInterfaces.emplace(InterfaceFinderWithLog{ InterfaceFinder{ clientDLL.getView(), retSpoofGadgets.client } }, retSpoofGadgets.client);
        const windows_platform::DynamicLibrary engineDLL{ windows_platform::DynamicLibraryWrapper{}, ENGINE_DLL };
        engineInterfaces.emplace(InterfaceFinderWithLog{ InterfaceFinder{ engineDLL.getView(), retSpoofGadgets.client } }, retSpoofGadgets.engine);
        interfaces.emplace(retSpoofGadgets.client);

        memory.emplace(Memory{ clientInterfaces->getClientAddress(), retSpoofGadgets });

        Netvars::init(clientInterfaces->getClient());
        gameEventListener.emplace(*memory, *clientInterfaces, *engineInterfaces, *interfaces);

        ImGui::CreateContext();
        ImGui_ImplWin32_Init(window);
        visuals.emplace(*memory, *interfaces, *clientInterfaces, *engineInterfaces);
        config.emplace(Config{ *visuals, *interfaces, *memory });
        gui.emplace(GUI{});
        hooks->install(*clientInterfaces, *interfaces, *memory);

        state = GlobalContext::State::Initialized;
    }

    return CallWindowProcW(hooks->originalWndProc, window, msg, wParam, lParam);
}

HRESULT GlobalContext::presentHook(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND windowOverride, const RGNDATA* dirtyRegion)
{
    [[maybe_unused]] static bool imguiInit{ ImGui_ImplDX9_Init(device) };

    if (config->loadScheduledFonts())
        ImGui_ImplDX9_DestroyFontsTexture();

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();

    renderFrame();

    if (device->BeginScene() == D3D_OK) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }

    //
    GameData::clearUnusedAvatars();
    InventoryChanger::clearUnusedItemIconTextures();
    //

    return hooks->originalPresent(device, src, dest, windowOverride, dirtyRegion);
}

#else
int GlobalContext::pollEventHook(SDL_Event* event)
{
    const auto result = hooks->pollEvent(event);

    if (state == GlobalContext::State::Initialized) {
        if (result && ImGui_ImplSDL2_ProcessEvent(event) && gui->isOpen())
            event->type = 0;
    } else if (state == GlobalContext::State::NotInitialized) {
        state = GlobalContext::State::Initializing;

        const linux_platform::SharedObject clientSo{ linux_platform::DynamicLibraryWrapper{}, CLIENT_DLL };
        clientInterfaces.emplace(InterfaceFinderWithLog{ InterfaceFinder{ clientSo.getView(), retSpoofGadgets.client } }, retSpoofGadgets.client);
        const linux_platform::SharedObject engineSo{ linux_platform::DynamicLibraryWrapper{}, ENGINE_DLL };
        engineInterfaces.emplace(InterfaceFinderWithLog{ InterfaceFinder{ engineSo.getView(), retSpoofGadgets.client } }, retSpoofGadgets.engine);

        interfaces.emplace(retSpoofGadgets.client);
        memory.emplace(Memory{ clientInterfaces->getClientAddress(), retSpoofGadgets });

        Netvars::init(clientInterfaces->getClient());
        gameEventListener.emplace(*memory, *clientInterfaces, *engineInterfaces, *interfaces);

        ImGui::CreateContext();
        visuals.emplace(*memory, *interfaces, *clientInterfaces, *engineInterfaces);
        config.emplace(Config{ *visuals, *interfaces, *memory });

        gui.emplace(GUI{});
        hooks->install(*clientInterfaces, *interfaces, *memory);

        state = GlobalContext::State::Initialized;
    }
    
    return result;
}

void GlobalContext::swapWindowHook(SDL_Window* window)
{
    [[maybe_unused]] static const auto _ = ImGui_ImplSDL2_InitForOpenGL(window, nullptr);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);

    renderFrame();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    GameData::clearUnusedAvatars();
    InventoryChanger::clearUnusedItemIconTextures();

    hooks->swapWindow(window);
}

#endif

void GlobalContext::viewModelSequenceNetvarHook(recvProxyData& data, void* outStruct, void* arg3)
{
    const auto viewModel = Entity::from(retSpoofGadgets.client, static_cast<csgo::pod::Entity*>(outStruct));

    if (localPlayer && clientInterfaces->getEntityList().getEntityFromHandle(viewModel.owner()) == localPlayer.get().getPOD()) {
        if (const auto weapon = Entity::from(retSpoofGadgets.client, clientInterfaces->getEntityList().getEntityFromHandle(viewModel.weapon())); weapon.getPOD() != nullptr) {
            if (visuals->isDeagleSpinnerOn() && weapon.getNetworkable().getClientClass()->classId == ClassId::Deagle && data.value._int == 7)
                data.value._int = 8;

            inventory_changer::InventoryChanger::instance(*interfaces, *memory).fixKnifeAnimation(weapon, data.value._int);
        }
    }

    proxyHooks.viewModelSequence.originalProxy(data, outStruct, arg3);
}

void GlobalContext::fireGameEventCallback(csgo::pod::GameEvent* eventPointer)
{
    const auto event = GameEvent::from(retSpoofGadgets.client, eventPointer);

    switch (fnv::hashRuntime(event.getName())) {
    case fnv::hash("round_start"):
        GameData::clearProjectileList();
        Misc::preserveKillfeed(*memory, true);
        [[fallthrough]];
    case fnv::hash("round_freeze_end"):
        Misc::purchaseList(engineInterfaces->getEngine(), *clientInterfaces, *interfaces, *memory, &event);
        break;
    case fnv::hash("player_death"): {
        auto& inventoryChanger = inventory_changer::InventoryChanger::instance(*interfaces, *memory);
        inventoryChanger.updateStatTrak(engineInterfaces->getEngine(), event);
        inventoryChanger.overrideHudIcon(engineInterfaces->getEngine(), *memory, event);
        Misc::killMessage(engineInterfaces->getEngine(), event);
        Misc::killSound(engineInterfaces->getEngine(), event);
        break;
    }
    case fnv::hash("player_hurt"):
        Misc::playHitSound(engineInterfaces->getEngine(), event);
        visuals->hitEffect(&event);
        visuals->hitMarker(&event);
        break;
    case fnv::hash("vote_cast"):
        Misc::voteRevealer(*clientInterfaces, *interfaces, *memory, event);
        break;
    case fnv::hash("round_mvp"):
        inventory_changer::InventoryChanger::instance(*interfaces, *memory).onRoundMVP(engineInterfaces->getEngine(), event);
        break;
    case fnv::hash("item_purchase"):
        Misc::purchaseList(engineInterfaces->getEngine(), *clientInterfaces, *interfaces, *memory, &event);
        break;
    case fnv::hash("bullet_impact"):
        visuals->bulletTracer(event);
        break;
    }
}

void GlobalContext::renderFrame()
{
    ImGui::NewFrame();

    if (const auto& displaySize = ImGui::GetIO().DisplaySize; displaySize.x > 0.0f && displaySize.y > 0.0f) {
        StreamProofESP::render(*memory, *config);
        Misc::purchaseList(engineInterfaces->getEngine(), *clientInterfaces, *interfaces, *memory);
        Misc::noscopeCrosshair(*memory, ImGui::GetBackgroundDrawList());
        Misc::recoilCrosshair(*memory, ImGui::GetBackgroundDrawList());
        Misc::drawOffscreenEnemies(engineInterfaces->getEngine(), *memory, ImGui::GetBackgroundDrawList());
        Misc::drawBombTimer(*memory);
        Misc::spectatorList();
        visuals->hitMarker(nullptr, ImGui::GetBackgroundDrawList());
        visuals->drawMolotovHull(ImGui::GetBackgroundDrawList());
        Misc::watermark(*memory);

        Aimbot::updateInput(*config);
        visuals->updateInput();
        StreamProofESP::updateInput(*config);
        Misc::updateInput();
        Triggerbot::updateInput(*config);
        Chams::updateInput(*config);
        Glow::updateInput();

        gui->handleToggle(*interfaces);

        if (gui->isOpen())
            gui->render(*visuals, engineInterfaces->getEngine(), *clientInterfaces, *interfaces, *memory, *config);
    }

    ImGui::EndFrame();
    ImGui::Render();
}
