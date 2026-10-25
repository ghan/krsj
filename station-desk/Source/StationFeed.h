// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mutex>
#if JUCE_MAC
#include <Security/Security.h>
#endif

// Only the background thread performs HTTP. Snapshots are idempotent and replace
// an older pending snapshot, so an outage cannot build an unbounded send queue.
class StationFeedPublisher final : private juce::Thread {
public:
    StationFeedPublisher() : Thread("Station website feed") { startThread(); }
    ~StationFeedPublisher() override { signalThreadShouldExit(); notify(); stopThread(7000); }
    void configure(juce::String url, juce::String secret) {
        std::lock_guard<std::mutex> lock(mutex);
        endpoint = url.trim(); token = secret;
        status = endpoint.isEmpty() ? "Website feed off" : token.isEmpty() ? "Website feed needs publisher key" : "Website feed ready";
    }
    void submit(juce::String json) {
        { std::lock_guard<std::mutex> lock(mutex); pending = std::move(json); }
        notify();
    }
    juce::String getStatus() { std::lock_guard<std::mutex> lock(mutex); return status; }
    static juce::String credential(juce::String replacement = {}, bool write = false) {
#if JUCE_MAC
        auto query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, CFSTR("org.krsj.StationDesk.feed"));
        CFDictionarySetValue(query, kSecAttrAccount, CFSTR("publisher"));
        juce::String result;
        if (write) {
            auto bytes = replacement.toUTF8();
            auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.getAddress()), replacement.getNumBytesAsUTF8());
            auto attrs = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFDictionarySetValue(attrs, kSecValueData, data);
            auto error = SecItemUpdate(query, attrs);
            if (error == errSecItemNotFound) {
                CFDictionarySetValue(query, kSecValueData, data); error = SecItemAdd(query, nullptr);
            }
            result = error == errSecSuccess ? "" : "Could not save feed key to Keychain (" + juce::String(int(error)) + ")";
            CFRelease(attrs); CFRelease(data);
        } else {
            CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
            // Reading preferences must never show a Keychain prompt at launch.
            CFDictionarySetValue(query, kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
            CFTypeRef data = nullptr;
            if (SecItemCopyMatching(query, &data) == errSecSuccess && data) {
                result = juce::String::fromUTF8(reinterpret_cast<const char*>(CFDataGetBytePtr(static_cast<CFDataRef>(data))), int(CFDataGetLength(static_cast<CFDataRef>(data))));
                CFRelease(data);
            }
        }
        CFRelease(query); return result;
#else
        juce::ignoreUnused(replacement, write); return {};
#endif
    }
private:
    std::mutex mutex;
    juce::String endpoint, token, pending, status { "Website feed off" };
    void run() override {
        for (;;) {
            juce::String url, secret, body;
            {
                std::lock_guard<std::mutex> lock(mutex);
                url = endpoint; secret = token; body = pending; pending.clear();
            }
            if (threadShouldExit() && body.isEmpty()) break;
            if (!url.isEmpty() && !secret.isEmpty() && !body.isEmpty()) {
                int code = 0;
                auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                    .withConnectionTimeoutMs(5000).withNumRedirectsToFollow(0).withStatusCode(&code)
                    .withExtraHeaders("Content-Type: application/json\r\nAuthorization: Bearer " + secret + "\r\n");
                auto input = juce::URL(url).withPOSTData(body).createInputStream(options);
                bool ok = input != nullptr && code >= 200 && code < 300;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    status = ok ? "Website feed connected" : "Website feed retrying (HTTP " + juce::String(code) + ")";
                    if (!ok && !threadShouldExit() && pending.isEmpty()) pending = body;
                }
            }
            if (!threadShouldExit()) wait(5000);
        }
    }
};
