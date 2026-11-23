#pragma once

#include <jni.h>

namespace Babylon::Embedding
{
    class Runtime;
    bool PackagedAudioEnabled(JNIEnv* env, jobject context);
    void SetPackagedAudioForeground(JNIEnv* env, bool foreground);
    void InstallPackagedAudio(JNIEnv* env, Runtime& runtime);
}
