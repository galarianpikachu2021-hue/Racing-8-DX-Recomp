# SPDX-License-Identifier: GPL-3.0-or-later
# Source this: Android toolchain for the fork's Android app. Uses the shared toolchain dir (KARTRECOMP_ANDROID_TOOLCHAIN, default
# "~/Claude Projects/toolchain") through a space-free symlink, because the NDK's build scripts break on paths with spaces.
_src="${KARTRECOMP_ANDROID_TOOLCHAIN:-$HOME/Claude Projects/toolchain}"
_link="${XDG_CACHE_HOME:-$HOME/.cache}/kartrecomp-android-toolchain"
[ -L "$_link" ] || ln -sfn "$_src" "$_link"
export JAVA_HOME="$_link/jdk17"
export ANDROID_HOME="$_link/android-sdk" ANDROID_SDK_ROOT="$_link/android-sdk"
export ANDROID_NDK_VERSION=28.2.13676358
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION"
export ANDROID_AVD_HOME="$_link/avd" ANDROID_USER_HOME="$_link/android-user" GRADLE_USER_HOME="$_link/gradle-home"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"
unset _src _link
# sdkmanager's own launcher resolves the real path and breaks on the space in it: call its entry point directly.
kr_sdkmanager() {
    local l="$ANDROID_HOME/cmdline-tools/latest"
    java -Dcom.android.sdkmanager.toolsdir="$l" -cp "$l/lib/*" com.android.sdklib.tool.sdkmanager.SdkManagerCli --sdk_root="$ANDROID_HOME" "$@"
}
