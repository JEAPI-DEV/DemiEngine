package dev.jeapi.demi.android;

import android.graphics.Rect;
import android.hardware.Sensor;
import android.content.Context;
import android.os.Bundle;
import android.os.Build;
import android.view.View;
import android.view.SurfaceHolder;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.inputmethod.InputMethodManager;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;

import org.json.JSONArray;
import org.json.JSONObject;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.util.ArrayList;
import java.util.HashMap;

public final class DemiActivity extends SDLActivity {
    private static native String nativeAccessibilitySnapshot();
    private static native void nativeAccessibilityAction(
            int type, String nodeId, float value, String text);
    private static native void nativeSurfaceAvailable(boolean available);

    @Override
    protected String[] getLibraries() {
        // SDL is linked statically into the engine runtime.
        return new String[] {"demi_android"};
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new DemiSurface(context);
    }

    private static final class DemiSurface extends SDLSurface {
        DemiSurface(Context context) {
            super(context);
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            super.surfaceCreated(holder);
            android.util.Log.i("DemiEngine", "[surface] Java surfaceCreated.");
            nativeSurfaceAvailable(true);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            android.util.Log.i("DemiEngine", "[surface] Java surfaceDestroyed.");
            nativeSurfaceAvailable(false);
            super.surfaceDestroyed(holder);
        }

        @Override
        protected void enableSensor(int sensorType, boolean enabled) {
            if (sensorType != Sensor.TYPE_ACCELEROMETER)
                super.enableSensor(sensorType, enabled);
        }
    }

    public void setDemiKeyboardVisible(final boolean visible) {
        runOnUiThread(() -> {
            View content = getWindow().getDecorView();
            InputMethodManager manager = (InputMethodManager)
                    getSystemService(Context.INPUT_METHOD_SERVICE);
            if (manager == null) return;
            if (visible) manager.showSoftInput(content, InputMethodManager.SHOW_IMPLICIT);
            else manager.hideSoftInputFromWindow(content.getWindowToken(), 0);
        });
    }

    public String getDemiDataPath() {
        return getFilesDir().getAbsolutePath();
    }

    public String getDemiCachePath() {
        return getCacheDir().getAbsolutePath();
    }

    private void applyImmersiveMode() {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    private void scheduleImmersiveMode() {
        View decor = getWindow().getDecorView();
        decor.post(() -> {
            SDLActivity.setWindowStyle(true);
            applyImmersiveMode();
        });
        decor.postDelayed(() -> {
            SDLActivity.setWindowStyle(true);
            applyImmersiveMode();
        }, 300);
        decor.postDelayed(this::applyImmersiveMode, 1000);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) scheduleImmersiveMode();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
                visibility -> {
                    if ((visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0)
                        getWindow().getDecorView().postDelayed(
                                this::applyImmersiveMode, 150);
                });
        scheduleImmersiveMode();
        final View content = getWindow().getDecorView();
        content.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        content.setAccessibilityDelegate(new View.AccessibilityDelegate() {
            private final DemiAccessibilityProvider provider =
                    new DemiAccessibilityProvider(content);

            @Override
            public AccessibilityNodeProvider getAccessibilityNodeProvider(View host) {
                return provider;
            }
        });
    }

    private static final class DemiAccessibilityProvider
            extends AccessibilityNodeProvider {
        private final View host;
        private final ArrayList<JSONObject> nodes = new ArrayList<>();
        private final HashMap<String, Integer> virtualIds = new HashMap<>();
        private float canvasWidth = 960.0f;
        private float canvasHeight = 540.0f;

        DemiAccessibilityProvider(View host) {
            this.host = host;
        }

        private void refresh() {
            nodes.clear();
            virtualIds.clear();
            try {
                JSONObject snapshot = new JSONObject(nativeAccessibilitySnapshot());
                canvasWidth = (float) snapshot.optDouble("canvas_width", 960.0);
                canvasHeight = (float) snapshot.optDouble("canvas_height", 540.0);
                JSONArray values = snapshot.optJSONArray("nodes");
                if (values == null) return;
                for (int index = 0; index < values.length(); ++index) {
                    JSONObject node = values.getJSONObject(index);
                    nodes.add(node);
                    virtualIds.put(node.optString("id"), index + 1);
                }
            } catch (Exception ignored) {
                // The renderer continues even if a transient native snapshot is absent.
            }
        }

        @Override
        public AccessibilityNodeInfo createAccessibilityNodeInfo(int virtualViewId) {
            refresh();
            if (virtualViewId == View.NO_ID || virtualViewId == HOST_VIEW_ID) {
                AccessibilityNodeInfo root = AccessibilityNodeInfo.obtain(host);
                host.onInitializeAccessibilityNodeInfo(root);
                root.setClassName("android.view.ViewGroup");
                for (int index = 0; index < nodes.size(); ++index) {
                    String parent = nodes.get(index).optString("parent");
                    if (parent.isEmpty() || !virtualIds.containsKey(parent))
                        root.addChild(host, index + 1);
                }
                return root;
            }
            final int index = virtualViewId - 1;
            if (index < 0 || index >= nodes.size()) return null;
            JSONObject node = nodes.get(index);
            AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain();
            info.setPackageName(host.getContext().getPackageName());
            info.setSource(host, virtualViewId);
            String parent = node.optString("parent");
            Integer parentId = virtualIds.get(parent);
            if (parentId == null) info.setParent(host);
            else info.setParent(host, parentId);
            String nodeId = node.optString("id");
            for (int childIndex = 0; childIndex < nodes.size(); ++childIndex) {
                if (nodeId.equals(nodes.get(childIndex).optString("parent")))
                    info.addChild(host, childIndex + 1);
            }
            info.setClassName(className(node.optString("role")));
            info.setText(node.optString("label"));
            info.setContentDescription(node.optString("description"));
            info.setEnabled(!node.optBoolean("disabled"));
            info.setFocusable(node.optBoolean("focusable"));
            info.setAccessibilityFocused(node.optBoolean("focused"));
            info.setCheckable("check_box".equals(node.optString("role")));
            info.setChecked(node.optBoolean("checked"));
            info.setClickable(isActivatable(node.optString("role")));
            if (info.isFocusable())
                info.addAction(AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS);
            if (info.isClickable()) info.addAction(AccessibilityNodeInfo.ACTION_CLICK);
            if ("scroll_area".equals(node.optString("role")) ||
                    "list".equals(node.optString("role"))) {
                info.setScrollable(true);
                info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
                info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
            }
            if ("text_field".equals(node.optString("role"))) {
                info.setEditable(true);
                info.addAction(AccessibilityNodeInfo.ACTION_SET_TEXT);
            }
            if ("slider".equals(node.optString("role"))) {
                info.setRangeInfo(AccessibilityNodeInfo.RangeInfo.obtain(
                        AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT,
                        (float) node.optDouble("minimum"),
                        (float) node.optDouble("maximum"),
                        (float) node.optDouble("value")));
                info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS);
            }
            float sx = host.getWidth() / Math.max(canvasWidth, 1.0f);
            float sy = host.getHeight() / Math.max(canvasHeight, 1.0f);
            Rect bounds = new Rect(
                    Math.round((float) node.optDouble("x") * sx),
                    Math.round((float) node.optDouble("y") * sy),
                    Math.round((float) (node.optDouble("x") +
                            node.optDouble("width")) * sx),
                    Math.round((float) (node.optDouble("y") +
                            node.optDouble("height")) * sy));
            info.setBoundsInParent(bounds);
            return info;
        }

        @Override
        public AccessibilityNodeInfo findFocus(int focus) {
            refresh();
            for (int index = 0; index < nodes.size(); ++index) {
                if (nodes.get(index).optBoolean("focused"))
                    return createAccessibilityNodeInfo(index + 1);
            }
            return null;
        }

        @Override
        public boolean performAction(int virtualViewId, int action, Bundle arguments) {
            refresh();
            int index = virtualViewId - 1;
            if (index < 0 || index >= nodes.size()) return false;
            String id = nodes.get(index).optString("id");
            if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS)
                nativeAccessibilityAction(0, id, 0, "");
            else if (action == AccessibilityNodeInfo.ACTION_CLICK)
                nativeAccessibilityAction(1, id, 0, "");
            else if (action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD)
                nativeAccessibilityAction(6, id, 0, "");
            else if (action == AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD)
                nativeAccessibilityAction(7, id, 0, "");
            else if (action == AccessibilityNodeInfo.ACTION_SET_TEXT) {
                CharSequence value = arguments == null ? "" : arguments.getCharSequence(
                        AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE);
                nativeAccessibilityAction(5, id, 0, value == null ? "" : value.toString());
            } else if (action == AccessibilityNodeInfo.AccessibilityAction
                    .ACTION_SET_PROGRESS.getId()) {
                float value = arguments == null ? 0.0f : arguments.getFloat(
                        AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE);
                nativeAccessibilityAction(4, id, value, "");
            } else return false;
            host.sendAccessibilityEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
            return true;
        }

        private static boolean isActivatable(String role) {
            return "button".equals(role) || "check_box".equals(role);
        }

        private static String className(String role) {
            if ("button".equals(role)) return "android.widget.Button";
            if ("check_box".equals(role)) return "android.widget.CheckBox";
            if ("slider".equals(role)) return "android.widget.SeekBar";
            if ("text_field".equals(role)) return "android.widget.EditText";
            if ("image".equals(role)) return "android.widget.ImageView";
            if ("static_text".equals(role)) return "android.widget.TextView";
            if ("list".equals(role)) return "android.widget.ListView";
            return "android.view.View";
        }
    }
}
