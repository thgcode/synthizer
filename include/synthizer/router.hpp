#pragma once

#include "synthizer/config.hpp"
#include "synthizer/memory.hpp"
#include "synthizer/types.hpp"

namespace synthizer {

namespace router {

/*
 * The router handles routing between sources and effects. Currently this means a one-to-many mapping of one level, but the below API
 * should be designed to be extensible enough to expand this to a full DAG later (i.e. echo feeds reverb). People will want that, but for V1
 * we choose to defer that functionality: to do it the router needs to start taking ownership of executing resources
 * and we'd have to implement a full directed acyclic graph, so we avoid it for now.
 * 
 * This is called a router because all the mixing actually happens outside of it: all this knows how to do is to hand out
 * buffers, such that one side is the reader and one side is the writer.
 * 
 * To make this work, we introduce the concept of InputHandle and OutputHandle, which represent the two sides of the relationship: a OutputHandle is used by a source
 * to feed any number of InputHandles.  Higher level Synthizer components will grab the InputHandle and OutputHandle of the lower level pieces they wish to route, find the router, and use APIs on it to do so.
 * 
 * Handles are immovable and use their addresses to uniquely identify themselves.
 * 
 * None of the components here are threadsafe.  It's anticipated that things will go through the normal command mechanism for now, with a queue of router-specific messages later.
 * */
class Router;

/*
 * An InputHandle is the reader side: i.e. an effect.
 * 
 * This is created pointed at a router and holds permanent configuration on the characteristics of the buffer.
 * */
class InputHandle {
	public:
	/* Configure the buffer to which audio is being routed. */
	InputHandle(Router *router, AudioSample *buffer, unsigned int channels);
	~InputHandle();
	InputHandle(const InputHandle &) = delete;

	private:
	friend class Router;
	friend class OutputHandle;
	/* NOTE: Routers set the routers in their handles to NULL on shutdown to avoid the overhead of deling with weak_ptr/shared_ptr. */
	Router *router = nullptr;
	AudioSample *buffer = nullptr;
	unsigned int channels = 0;
};

/*
 * The output is the source, etc.
 * */
class OutputHandle {
	public:
	OutputHandle(Router *router);
	~OutputHandle();
	OutputHandle(const OutputHandle &) = delete;

	/*
	 * Given an input buffer of audio data, route it to the appropriate destinations.
	 * */
	void routeAudio(AudioSample *buffer, unsigned int channels);

	private:
	friend class Router;
	/* NOTE: Routers set the routers in their handles to NULL on shutdown to avoid the overhead of deling with weak_ptr/shared_ptr. */
	Router *router = nullptr;
};

enum class RouteState {
	/* The route was created and has requested a fade-in. */
	FadeIn,
	/* The route is fading out, and will die. */
	FadeOut,
	/* The route has reached a steady state. */
 Steady,
 /* The gain has been changed, but the router isn't fading in or out. */
 GainChanged,
 /* The route has died. Remove it on the next mainloop iteration. */
 Dead,
};

/*
 * Internal type for an audio route.
 * */
class Route {
	public:
	/* Whether configuration changes should take effect. Goes to false if this route is in the process of dying. */
	bool canConfigure();
	void setGain(float gain, unsigned int time_block);
	void setState(RouteState state, unsigned int block_time);

	InputHandle *input = nullptr;
	OutputHandle *output = nullptr;
	RouteState state = RouteState::Dead;
	/* A router-local per-block timestamp of this route's last state transition time. */
	unsigned int last_state_changed = 0;
	/* When fading in, how many blocks should we fade in over? Because Synthizer is small, we keep this simple and use whole blocks only, for now. */
	unsigned int fade_in_blocks = 1;
	/* Same as fade_in_blocks but for fade-out. */
	unsigned int fade_out_blocks = 1;
	/* When we're in the steady state, what should our gain be? */
	float gain = 1.0f;
	/* When the gain just changed, what was the previous value? */
	float prev_gain = 1.0f;
};

class Router {
	public:
	Router() = default;
	~Router();
	Router(const Router&) = delete;

	/*
	 * Establish or update a route with specified gain and fade-in.
	 * View this as a declarative interface. if w->r doesn't exist, it gets added, with the specified fadein time, otherwise the gain is updated.
	 * */
	void configureRoute(OutputHandle *output, InputHandle *input, float gain = 1.0, unsigned int fade_in = 1);
	/* Remove a route. If it doesn't exist, do nothing. */
	void removeRoute(OutputHandle *output, InputHandle *input, unsigned int fade_out = 1);
	/* Remove all routes for a specified WriteHandle. */
	void removeAllRoutes(OutputHandle *output, unsigned int fade_out = 1);
	/* Signal the router that we are finished with a block of audio. Increments the internal timestamp. */
	void finishBlock();

	private:
	friend class InputHandle;
	friend class OutputHandle;

	void unregisterInputHandle(InputHandle *handle);
	void unregisterOutputHandle(OutputHandle *handle);

 /* Returns routes.end() if not found. */
	deferred_vector<Route>::iterator findRouteForPair(OutputHandle *output, InputHandle *input);

	/* Returns iterator to the beginning of the runfor the specified output. */
	deferred_vector<Route>::iterator findRun(OutputHandle *output);

	deferred_vector<Route> routes;
	unsigned int time = 0;
};

} /* namespace router */
} /* namespace synthizer */