package dev.jeapi.demi.android;

import org.libsdl.app.SDLActivity;

public final class DemiActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // SDL is linked statically into the engine runtime.
        return new String[] {"demi_android"};
    }
}
