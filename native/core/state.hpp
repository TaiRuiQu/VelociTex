// The state that the renderer reuses between renders.
//
// The .NET endpoint renders from several threads, so the state has to belong
// to the thread that uses it.  A `thread_local` whose type has a constructor
// carries an initialization guard on every access, and the guard is a call
// into the runtime.  In WebAssembly that call costs more than the access
// itself: the state of the MathML writer and of the node arena is touched
// hundreds of times per formula.
//
// The state is therefore declared at namespace scope.  In the WebAssembly
// build it is a plain static, which the module initializes before it runs, so
// an access to it costs nothing.  The WebAssembly module is single threaded,
// so the state does not have to be per thread there; a build with threads
// keeps the thread local.
#pragma once

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define VELOCITEX_REUSED_STATE(T, name) static T name
// The same for a static member of a class.
#define VELOCITEX_REUSED_MEMBER static inline
#else
#define VELOCITEX_REUSED_STATE(T, name) static thread_local T name
#define VELOCITEX_REUSED_MEMBER static inline thread_local
#endif
