#pragma once
#include "body_position.h"
#include "core.h"
#include "owned_value.h"
#include <RE/Skyrim.h>
#include <array>
#include <chrono>
#include <cmath>

namespace first_person {
enum class Status { inactive, applied, missing_body, missing_native_arms, unsupported_skeleton, invalid_transform, invalid_alignment, publication_failed };

struct AlignmentSample {
    BodyAlignmentFrame frame{};
    BodyAlignmentResult result{};
    bool available = false;
};

// Opt-in native-first-person rendering experiment. The coordinator must authorize
// normal first person; this class never forces POV, moves the camera or patches
// rendering. Full shadows, interior visibility and equipment still need visual QA.
class Renderer {
    struct Hidden {
        RE::NiPointer<RE::NiAVObject> node;
        camera::OwnedValue<bool> value;
        void Restore() {
            if (!node) return;
            bool current = node->GetAppCulled();
            if (value.Restore(current)) node->SetAppCulled(current);
            node.reset();
        }
    };
    RE::NiPointer<RE::NiAVObject> body_;
    RE::NiPointer<RE::NiAVObject> nativeArms_;
    RE::NiPointer<RE::NiAVObject> eyeNode_;
    std::array<RE::NiPointer<RE::NiAVObject>, 3> bones_;
    std::array<camera::OwnedValue<float>, 3> scales_;
    std::array<Hidden, 20> hidden_;
    camera::OwnedValue<bool> bodyVisibility_;
    camera::OwnedValue<bool> nativeVisibility_;
    camera::OwnedValue<RE::NiPoint3> bodyPosition_;
    RE::NiPointer<RE::NiNode> positionParent_;
    RE::NiPointer<RE::NiNode> renderedParent_;
    RE::NiTransform renderedBody_{};
    std::array<RE::NiTransform, 3> renderedBones_{};
    bool renderWorldDirty_ = false;
    body_position::PublicationSample publicationSample_{};
    AlignmentSample alignmentSample_{};
    double lastAlignmentMathUs_ = 0.0;
    bool usesNativeArms_ = true;
    Status status_ = Status::inactive;

    static bool Finite(const RE::NiTransform& transform) {
        if (!std::isfinite(transform.scale) || transform.scale <= 0.0f) return false;
        const auto& p = transform.translate;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
        for (const auto& row : transform.rotate.entry)
            for (float value : row) if (!std::isfinite(value)) return false;
        return true;
    }

    bool Attached(const RE::NiAVObject* node) const {
        // A bounded ancestry check handles retained nodes detached by equipment or
        // skeleton replacement without following a malformed chain indefinitely.
        for (unsigned depth = 0; node && depth < 64; ++depth, node = node->parent)
            if (node == body_.get()) return true;
        return false;
    }

    void Hide(RE::NiAVObject* node) {
        if (!node || node == body_.get() || !Attached(node)) return;
        for (const auto& entry : hidden_) if (entry.node.get() == node) return;
        for (auto& entry : hidden_) {
            if (entry.node) continue;
            bool hidden = node->GetAppCulled();
            if (!entry.value.Write(hidden, true)) return;
            entry.node.reset(node);
            node->SetAppCulled(hidden);
            return;
        }
    }

    void UpdateTransforms() {
        // The native selected-transform pass can skip changed local transforms.
        // Update's full downward pass always publishes them. Keep kDirty clear:
        // the verified NiNode pass runs controllers only when that bit is set.
        if (!body_ || !Finite(body_->local) || !Finite(body_->world)) return;
        RE::NiUpdateData update{};
        update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
        body_->Update(update);
    }

    bool OwnsRenderedPose() const {
        if (!renderWorldDirty_ || !body_) return false;
        std::array<RE::NiTransform, 3> current{};
        for (std::size_t i = 0; i < bones_.size(); ++i)
            if (!bones_[i] || !Attached(bones_[i].get())) return false;
            else current[i] = bones_[i]->world;
        return body_position::OwnsPublication(renderedBody_, body_->world, renderedBones_, current,
            renderedParent_.get(), body_->parent);
    }

public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] Status GetStatus() const { return status_; }
    [[nodiscard]] const AlignmentSample& GetAlignmentSample() const { return alignmentSample_; }
    [[nodiscard]] double AlignmentMathMicroseconds() const { return lastAlignmentMathUs_; }
    [[nodiscard]] const body_position::PublicationSample& GetPublicationSample() const { return publicationSample_; }
    [[nodiscard]] bool UsesNativeArms() const { return usesNativeArms_; }

    // Call before the chained native first-person update and on every exit/load.
    // NiPointer references keep any replaced skeleton alive until leases release.
    void Restore() {
        const bool restoreWorld = OwnsRenderedPose();
        bool transformsChanged = false;
        if (body_) transformsChanged |= body_position::RestoreTranslation(bodyPosition_,
            body_->local.translate, positionParent_.get(), body_->parent);
        positionParent_.reset();
        for (std::size_t i = 0; i < bones_.size(); ++i)
            if (bones_[i]) transformsChanged |= scales_[i].Restore(bones_[i]->local.scale);
        for (auto& entry : hidden_) entry.Restore();
        if (nativeArms_) {
            bool hidden = nativeArms_->GetAppCulled();
            if (nativeVisibility_.Restore(hidden)) nativeArms_->SetAppCulled(hidden);
        }
        if (body_) {
            bool hidden = body_->GetAppCulled();
            if (bodyVisibility_.Restore(hidden)) body_->SetAppCulled(hidden);
        }
        // The locals have already been returned to the native pose after publish.
        // Rebuild derived world transforms only while our captured outputs remain
        // ours. A native animation update or another writer takes precedence.
        if (restoreWorld || (!renderWorldDirty_ && transformsChanged)) UpdateTransforms();
        renderWorldDirty_ = false;
        renderedParent_.reset();
        status_ = Status::inactive;
    }

    void Reset() {
        Restore();
        for (auto& bone : bones_) bone.reset();
        nativeArms_.reset();
        eyeNode_.reset();
        body_.reset();
    }

    Status Apply(RE::PlayerCharacter* player, const RE::NiPoint3& viewEye,
        const BodyAlignmentOptions& options, const RE::NiCamera* cameraObject,
        bool measureMath = false) {
        Restore();
        alignmentSample_.available = false;
        publicationSample_.available = false;
        lastAlignmentMathUs_ = 0.0;
        auto* body = player ? player->Get3D(false) : nullptr;
        auto* arms = player ? player->Get3D(true) : nullptr;
        bool rebuild = body != body_.get() || arms != nativeArms_.get();
        // Some skeleton providers replace only children under a persistent root.
        // A detached cached bone is safe to retain, but cannot identify its new
        // replacement; reacquire the capability set before returning to rendering.
        if (!rebuild && body)
            for (const auto& bone : bones_) rebuild |= !bone || !Attached(bone.get());
        if (rebuild) {
            Reset();
            body_.reset(body);
            nativeArms_.reset(arms);
            if (body_) {
                // Conventional humanoid names are capabilities, not assumptions.
                // An unknown skeleton retains native visibility without any writes.
                static const std::array<RE::BSFixedString, 3> names{
                    "NPC Head [Head]", "NPC L UpperArm [LUar]", "NPC R UpperArm [RUar]"};
                for (std::size_t i = 0; i < bones_.size(); ++i)
                    bones_[i].reset(body_->GetObjectByName(names[i]));
            }
        }
        if (!body_) return status_ = Status::missing_body;
        if (!nativeArms_ || !body_position::IndependentRoots(body_.get(), nativeArms_.get()))
            return status_ = Status::missing_native_arms;
        if (!body_position::IndependentRoots(body_.get(), static_cast<const RE::NiAVObject*>(cameraObject)))
            return status_ = Status::invalid_alignment;
        if (!Finite(body_->local) || !Finite(body_->world)) return status_ = Status::invalid_transform;
        // A native producer may replace just part of our previous world pose.
        // Never treat such mixed output as the next unmodified alignment input.
        if (!body_position::WorldMatchesLocal(body_->local, body_->world,
                body_->parent ? &body_->parent->world : nullptr, body_->world.scale))
            return status_ = Status::invalid_transform;
        for (const auto& bone : bones_) {
            if (!bone || !Attached(bone.get())) return status_ = Status::unsupported_skeleton;
            if (!Finite(bone->local) || !Finite(bone->world)) return status_ = Status::invalid_transform;
            if (!body_position::WorldMatchesLocal(bone->local, bone->world,
                    bone->parent ? &bone->parent->world : nullptr, body_->world.scale))
                return status_ = Status::invalid_transform;
        }
        if (!eyeNode_ || !Attached(eyeNode_.get())) {
            static const RE::BSFixedString eyeName("NPCEyeBone");
            eyeNode_.reset(body_->GetObjectByName(eyeName));
        }
        const bool eyeAvailable = eyeNode_ && Attached(eyeNode_.get()) &&
            body_position::WorldMatchesLocal(eyeNode_->local, eyeNode_->world,
                eyeNode_->parent ? &eyeNode_->parent->world : nullptr, body_->world.scale);

        // The displayed camera includes native dampening and collision. Its
        // caller reconciles both model and final-view updates against that eye.
        // Capture unmasked landmarks before shrinking any bones.
        const auto& head = bones_[0]->world.translate;
        std::array<float, 2> heading{};
        if (!body_position::ViewHeading(cameraObject->world.rotate, heading))
            return status_ = Status::invalid_alignment;
        const auto eye = eyeAvailable ? eyeNode_->world.translate : RE::NiPoint3{};
        const BodyAlignmentFrame frame{{viewEye.x, viewEye.y, viewEye.z},
            {head.x, head.y, head.z}, {heading[0], heading[1]}, body_->world.scale,
            {eye.x, eye.y, eye.z}, eyeAvailable ? 1u : 0u};
        BodyAlignmentResult alignment{};
        if (measureMath) {
            const auto started = std::chrono::steady_clock::now();
            alignment = cc_align_body(frame, options);
            lastAlignmentMathUs_ = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - started).count();
        } else alignment = cc_align_body(frame, options);
        alignmentSample_ = {frame, alignment, true};
        RE::NiPoint3 translated = body_->local.translate;
        if (!alignment.valid || !body_position::Translate(body_->local.translate,
                {alignment.translation[0], alignment.translation[1], alignment.translation[2]},
                body_->parent ? &body_->parent->world : nullptr, translated))
            return status_ = Status::invalid_alignment;

        RE::NiTransform expectedBody = body_->world;
        expectedBody.translate = body_->parent ? body_->parent->world * translated : translated;
        if (!Finite(expectedBody)) return status_ = Status::invalid_transform;
        const auto shift = expectedBody.translate - body_->world.translate;
        bool equippedLight = false;
        for (bool left : {false, true})
            if (const auto* item = player->GetEquippedObject(left); item && item->Is(RE::FormType::Light))
                equippedLight = true;
        // ActorState's base offset varies with the runtime. An inherited call on
        // PlayerCharacter would use the compile-time layout in this multi-build.
        const auto armsPolicy = body_position::SelectArms(player->AsActorState()->GetWeaponState(), equippedLight);
        usesNativeArms_ = !armsPolicy.hideNative;
        std::array<RE::NiTransform, 3> expectedBones{};
        for (std::size_t i = 0; i < bones_.size(); ++i) {
            expectedBones[i] = bones_[i]->world;
            expectedBones[i].translate += shift;
            expectedBones[i].scale *= armsPolicy.boneFactors[i];
        }

        // All required nodes and values passed validation before the first write.
        // The actor's 3D is also read by other engine systems: this is a reversible
        // scene translation, not a guarantee that equipment/projectiles are neutral.
        if (bodyPosition_.Write(body_->local.translate, translated)) positionParent_.reset(body_->parent);
        // Select one arm source. Keeping the full body arms while sheathed avoids
        // collapsing shoulder vertices weighted partly to the upper-arm bones.
        for (std::size_t i = 0; i < bones_.size(); ++i)
            scales_[i].Write(bones_[i]->local.scale, bones_[i]->local.scale * armsPolicy.boneFactors[i]);
        if (armsPolicy.hideNative) {
            bool hidden = nativeArms_->GetAppCulled();
            if (nativeVisibility_.Write(hidden, true)) nativeArms_->SetAppCulled(hidden);
        }
        if (const auto& biped = player->GetBiped(false); biped) {
            for (auto slot : {RE::BIPED_OBJECTS::kHead, RE::BIPED_OBJECTS::kHair,
                    RE::BIPED_OBJECTS::kLongHair, RE::BIPED_OBJECTS::kCirclet,
                    RE::BIPED_OBJECTS::kEars,
                    RE::BIPED_OBJECTS::kDecapitateHead})
                Hide(biped->objects[slot].partClone.get());
            // Equipment is not a head mask. Preserve holstered weapons, quivers
            // and shields rather than culling every biped equipment clone.
            // Held body equipment follows its selected arm's bone transform.
        }
        Hide(player->GetFaceNodeSkinned());
        bool hidden = body_->GetAppCulled();
        if (bodyVisibility_.Write(hidden, false)) body_->SetAppCulled(hidden);
        UpdateTransforms();
        renderedBody_ = body_->world;
        renderedParent_.reset(body_->parent);
        for (std::size_t i = 0; i < bones_.size(); ++i) renderedBones_[i] = bones_[i]->world;
        renderWorldDirty_ = true;
        publicationSample_ = body_position::CheckPublication(expectedBody, renderedBody_,
            expectedBones, renderedBones_, frame.scale);
        // Rendering consumes the published world transforms; native animation
        // must never inherit our shrunken local bone scales on its next update.
        for (std::size_t i = 0; i < bones_.size(); ++i) scales_[i].Restore(bones_[i]->local.scale);
        // Root translation also belongs only to this publication. Leaving it in
        // local space could feed the next producer our rendered displacement.
        body_position::RestoreTranslation(bodyPosition_, body_->local.translate,
            positionParent_.get(), body_->parent);
        positionParent_.reset();
        if (!publicationSample_.matched) {
            Restore();
            return status_ = Status::publication_failed;
        }
        return status_ = Status::applied;
    }
};
}
