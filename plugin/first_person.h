#pragma once
#include "body_position.h"
#include "core.h"
#include "owned_value.h"
#include <RE/Skyrim.h>
#include <array>
#include <chrono>
#include <cmath>

namespace first_person {
enum class Status { inactive, applied, missing_body, missing_native_arms, unsupported_skeleton, invalid_transform, invalid_alignment };

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
    std::array<RE::NiPointer<RE::NiAVObject>, 3> bones_;
    std::array<camera::OwnedValue<float>, 3> scales_;
    std::array<Hidden, 20> hidden_;
    camera::OwnedValue<bool> bodyVisibility_;
    camera::OwnedValue<RE::NiPoint3> bodyPosition_;
    RE::NiPointer<RE::NiNode> positionParent_;
    AlignmentSample alignmentSample_{};
    double lastAlignmentMathUs_ = 0.0;
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
        // Update skinning transforms/bounds only, without advancing animation or
        // running a second full NiAVObject::Update with another frame delta.
        if (!body_ || !Finite(body_->local) || !Finite(body_->world)) return;
        RE::NiUpdateData update{};
        update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
        body_->UpdateTransformAndBounds(update);
    }

public:
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] Status GetStatus() const { return status_; }
    [[nodiscard]] const AlignmentSample& GetAlignmentSample() const { return alignmentSample_; }
    [[nodiscard]] double AlignmentMathMicroseconds() const { return lastAlignmentMathUs_; }

    // Call before the chained native first-person update and on every exit/load.
    // NiPointer references keep any replaced skeleton alive until leases release.
    void Restore() {
        bool transformsChanged = false;
        if (body_) transformsChanged |= body_position::RestoreTranslation(bodyPosition_,
            body_->local.translate, positionParent_.get(), body_->parent);
        positionParent_.reset();
        for (std::size_t i = 0; i < bones_.size(); ++i)
            if (bones_[i]) transformsChanged |= scales_[i].Restore(bones_[i]->local.scale);
        for (auto& entry : hidden_) entry.Restore();
        if (body_) {
            bool hidden = body_->GetAppCulled();
            if (bodyVisibility_.Restore(hidden)) body_->SetAppCulled(hidden);
        }
        if (transformsChanged) UpdateTransforms();
        status_ = Status::inactive;
    }

    void Reset() {
        Restore();
        for (auto& bone : bones_) bone.reset();
        nativeArms_.reset();
        body_.reset();
    }

    Status Apply(RE::PlayerCharacter* player, const RE::NiPoint3& nativeEye,
        const BodyAlignmentOptions& options, const RE::NiAVObject* cameraObject,
        bool measureMath = false) {
        Restore();
        alignmentSample_.available = false;
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
        if (!body_position::IndependentRoots(body_.get(), cameraObject))
            return status_ = Status::invalid_alignment;
        if (!Finite(body_->local) || !Finite(body_->world)) return status_ = Status::invalid_transform;
        for (const auto& bone : bones_) {
            if (!bone || !Attached(bone.get())) return status_ = Status::unsupported_skeleton;
            if (!Finite(bone->local) || !Finite(bone->world)) return status_ = Status::invalid_transform;
        }

        // Native eye is sampled from FirstPersonState::GetTranslation after the
        // chained update. Capture the unmasked head before shrinking any bones.
        const auto& head = bones_[0]->world.translate;
        const float yaw = player->GetAngleZ();
        const BodyAlignmentFrame frame{{nativeEye.x, nativeEye.y, nativeEye.z},
            {head.x, head.y, head.z}, {std::sin(yaw), std::cos(yaw)}, body_->world.scale};
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

        // All required nodes and values passed validation before the first write.
        // The actor's 3D is also read by other engine systems: this is a reversible
        // scene translation, not a guarantee that equipment/projectiles are neutral.
        if (bodyPosition_.Write(body_->local.translate, translated)) positionParent_.reset(body_->parent);
        // Mask weighted head/arm vertices while retaining Skyrim's native 1P arms.
        for (std::size_t i = 0; i < bones_.size(); ++i)
            scales_[i].Write(bones_[i]->local.scale, bones_[i]->local.scale * 0.0001f);
        if (const auto& biped = player->GetBiped(false); biped) {
            for (auto slot : {RE::BIPED_OBJECTS::kHead, RE::BIPED_OBJECTS::kHair,
                    RE::BIPED_OBJECTS::kLongHair, RE::BIPED_OBJECTS::kCirclet,
                    RE::BIPED_OBJECTS::kEars, RE::BIPED_OBJECTS::kShield,
                    RE::BIPED_OBJECTS::kDecapitateHead})
                Hide(biped->objects[slot].partClone.get());
            for (std::uint32_t slot = RE::BIPED_OBJECTS::kHandToHandMelee;
                    slot < RE::BIPED_OBJECTS::kTotal; ++slot)
                Hide(biped->objects[slot].partClone.get());
        }
        Hide(player->GetFaceNodeSkinned());
        bool hidden = body_->GetAppCulled();
        if (bodyVisibility_.Write(hidden, false)) body_->SetAppCulled(hidden);
        UpdateTransforms();
        return status_ = Status::applied;
    }
};
}
