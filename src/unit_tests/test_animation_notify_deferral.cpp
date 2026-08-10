// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/animation.h"
#include "scene_graph/animation_notify.h"
#include "scene_graph/animation_state.h"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	struct TriggeredNotify
	{
		AnimationNotifyType type;
		String animationName;
	};

	/// Builds a 1 second animation with a single footstep notify and an enabled
	/// animation state bound to it, recording every emitted notify.
	struct NotifyTestFixture
	{
		Animation animation{ "Attack", 1.0f };
		AnimationStateSet stateSet;
		AnimationState* state{ nullptr };
		std::vector<TriggeredNotify> triggered;
		scoped_connection connection;

		explicit NotifyTestFixture(const float notifyTime)
		{
			auto notify = std::make_unique<FootstepNotify>();
			notify->SetTime(notifyTime);
			animation.AddNotify(std::move(notify));

			state = stateSet.CreateAnimationState("Attack", 0.0f, 1.0f, 1.0f, true);
			state->SetAnimation(&animation);
			connection = state->notifyTriggered.connect(
				[this](const AnimationNotify& notify, const String& animationName, const AnimationState&)
				{
					triggered.push_back({ notify.GetType(), animationName });
				});
		}
	};
}

TEST_CASE("AnimationState emits notifies immediately while deferral is inactive", "[animation_notify]")
{
	NotifyTestFixture fixture(0.5f);

	fixture.state->AddTime(0.6f);

	REQUIRE(fixture.triggered.size() == 1);
	CHECK(fixture.triggered[0].type == AnimationNotifyType::Footstep);
}

TEST_CASE("AnimationState defers notify emission while deferral is active", "[animation_notify]")
{
	NotifyTestFixture fixture(0.5f);

	AnimationState::SetNotifyDeferralEnabled(true);
	fixture.state->AddTime(0.6f);
	AnimationState::SetNotifyDeferralEnabled(false);

	// Nothing may be emitted while clip times advance on worker threads.
	REQUIRE(fixture.triggered.empty());

	// Flushing on the main thread delivers the collected notify exactly once.
	fixture.stateSet.FlushDeferredNotifies();
	REQUIRE(fixture.triggered.size() == 1);
	CHECK(fixture.triggered[0].type == AnimationNotifyType::Footstep);
	CHECK(fixture.triggered[0].animationName == "Attack");

	// A second flush must not re-emit.
	fixture.stateSet.FlushDeferredNotifies();
	CHECK(fixture.triggered.size() == 1);
}

TEST_CASE("Deferred notifies do not re-trigger on later advances", "[animation_notify]")
{
	NotifyTestFixture fixture(0.5f);

	AnimationState::SetNotifyDeferralEnabled(true);
	fixture.state->AddTime(0.6f);
	AnimationState::SetNotifyDeferralEnabled(false);
	fixture.stateSet.FlushDeferredNotifies();
	REQUIRE(fixture.triggered.size() == 1);

	// Advancing further without crossing the notify again must not emit.
	fixture.state->AddTime(0.2f);
	CHECK(fixture.triggered.size() == 1);
}

TEST_CASE("Removing a pending state before the flush drops its notifies safely", "[animation_notify]")
{
	NotifyTestFixture fixture(0.5f);

	AnimationState::SetNotifyDeferralEnabled(true);
	fixture.state->AddTime(0.6f);
	AnimationState::SetNotifyDeferralEnabled(false);

	fixture.stateSet.RemoveAnimationState("Attack");

	// The removed state's collected notifies must be dropped, not emitted from
	// a destroyed state.
	fixture.stateSet.FlushDeferredNotifies();
	CHECK(fixture.triggered.empty());
}

TEST_CASE("A handler removing another pending state during the flush skips it safely", "[animation_notify]")
{
	Animation animationA{ "AttackA", 1.0f };
	Animation animationB{ "AttackB", 1.0f };

	auto notifyA = std::make_unique<FootstepNotify>();
	notifyA->SetTime(0.5f);
	animationA.AddNotify(std::move(notifyA));

	auto notifyB = std::make_unique<FootstepNotify>();
	notifyB->SetTime(0.5f);
	animationB.AddNotify(std::move(notifyB));

	AnimationStateSet stateSet;
	AnimationState* stateA = stateSet.CreateAnimationState("AttackA", 0.0f, 1.0f, 1.0f, true);
	stateA->SetAnimation(&animationA);
	AnimationState* stateB = stateSet.CreateAnimationState("AttackB", 0.0f, 1.0f, 1.0f, true);
	stateB->SetAnimation(&animationB);

	std::vector<String> triggered;
	const scoped_connection connectionA{ stateA->notifyTriggered.connect(
		[&triggered, &stateSet](const AnimationNotify&, const String& animationName, const AnimationState&)
		{
			triggered.push_back(animationName);

			// Destroys stateB while the flush loop still holds a pointer to it.
			stateSet.RemoveAnimationState("AttackB");
		}) };
	const scoped_connection connectionB{ stateB->notifyTriggered.connect(
		[&triggered](const AnimationNotify&, const String& animationName, const AnimationState&)
		{
			triggered.push_back(animationName);
		}) };

	AnimationState::SetNotifyDeferralEnabled(true);
	stateA->AddTime(0.6f);
	stateB->AddTime(0.6f);
	AnimationState::SetNotifyDeferralEnabled(false);

	stateSet.FlushDeferredNotifies();

	// Only stateA's notify may fire: its handler destroyed stateB, so the flush
	// must skip the destroyed state instead of touching freed memory.
	REQUIRE(triggered.size() == 1);
	CHECK(triggered[0] == "AttackA");
}

TEST_CASE("Looping across a notify defers one emission per crossing", "[animation_notify]")
{
	NotifyTestFixture fixture(0.5f);
	fixture.state->SetLoop(true);

	AnimationState::SetNotifyDeferralEnabled(true);
	fixture.state->AddTime(0.6f); // Crosses the notify at 0.5.
	fixture.state->AddTime(0.6f); // Wraps to 0.2 without crossing it.
	fixture.state->AddTime(0.4f); // Crosses the notify at 0.5 again.
	AnimationState::SetNotifyDeferralEnabled(false);

	REQUIRE(fixture.triggered.empty());
	fixture.stateSet.FlushDeferredNotifies();
	CHECK(fixture.triggered.size() == 2);
}
