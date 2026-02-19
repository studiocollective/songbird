#include "WebViewHelpers.h"

#if JUCE_MAC

#include <objc/objc.h>
#include <objc/message.h>
#include <objc/runtime.h>

// Cast helper — objc_msgSend has different signatures per call
template <typename Ret, typename... Args>
static inline Ret msg(id obj, SEL sel, Args... args)
{
    return ((Ret (*)(id, SEL, Args...))objc_msgSend)(obj, sel, args...);
}

//==============================================================================
// Inspector
//==============================================================================

void enableWebViewInspector(juce::Component* webViewComponent)
{
#if JUCE_DEBUG
    auto* peer = webViewComponent->getPeer();
    if (!peer) return;

    auto nsView = (id) peer->getNativeHandle();
    id subviews = msg<id>(nsView, sel_registerName("subviews"));
    if (!subviews) return;

    auto count = msg<unsigned long>(subviews, sel_registerName("count"));
    SEL inspSel = sel_registerName("setInspectable:");

    for (unsigned long i = 0; i < count; i++)
    {
        id child = msg<id>(subviews, sel_registerName("objectAtIndex:"), i);
        if (!child) continue;

        if (msg<BOOL>(child, sel_registerName("respondsToSelector:"), inspSel))
        {
            msg<void>(child, inspSel, (BOOL)YES);
            DBG("WebView inspector enabled");
            return;
        }

        // Check one level deeper
        id childSubs = msg<id>(child, sel_registerName("subviews"));
        if (!childSubs) continue;
        auto childCount = msg<unsigned long>(childSubs, sel_registerName("count"));
        for (unsigned long j = 0; j < childCount; j++)
        {
            id gc = msg<id>(childSubs, sel_registerName("objectAtIndex:"), j);
            if (gc && msg<BOOL>(gc, sel_registerName("respondsToSelector:"), inspSel))
            {
                msg<void>(gc, inspSel, (BOOL)YES);
                DBG("WebView inspector enabled");
                return;
            }
        }
    }
#else
    juce::ignoreUnused(webViewComponent);
#endif
}

//==============================================================================
// WKWebView lookup
//==============================================================================

static id findWKWebView(juce::Component* webViewComponent)
{
    auto* peer = webViewComponent->getPeer();
    if (!peer) return nil;

    auto nsView = (id) peer->getNativeHandle();
    id subviews = msg<id>(nsView, sel_registerName("subviews"));
    if (!subviews) return nil;

    auto count = msg<unsigned long>(subviews, sel_registerName("count"));
    SEL pageSel = sel_registerName("setPageZoom:");

    for (unsigned long i = 0; i < count; i++)
    {
        id child = msg<id>(subviews, sel_registerName("objectAtIndex:"), i);
        if (!child) continue;

        if (msg<BOOL>(child, sel_registerName("respondsToSelector:"), pageSel))
            return child;

        id childSubs = msg<id>(child, sel_registerName("subviews"));
        if (!childSubs) continue;
        auto childCount = msg<unsigned long>(childSubs, sel_registerName("count"));
        for (unsigned long j = 0; j < childCount; j++)
        {
            id gc = msg<id>(childSubs, sel_registerName("objectAtIndex:"), j);
            if (gc && msg<BOOL>(gc, sel_registerName("respondsToSelector:"), pageSel))
                return gc;
        }
    }
    return nil;
}

//==============================================================================
// Zoom
//==============================================================================

void setWebViewPageZoom(juce::Component* webViewComponent, double zoom)
{
    id wkView = findWKWebView(webViewComponent);
    if (wkView)
    {
        msg<void>(wkView, sel_registerName("setPageZoom:"), zoom);
        DBG("WebView page zoom set to " + juce::String(zoom));
    }
}

#endif
