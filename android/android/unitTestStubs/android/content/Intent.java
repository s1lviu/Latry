package android.content;

import java.util.HashMap;
import java.util.Map;

public class Intent {
    public static final String ACTION_BOOT_COMPLETED = "android.intent.action.BOOT_COMPLETED";
    public static final String ACTION_MEDIA_BUTTON = "android.intent.action.MEDIA_BUTTON";
    public static final String EXTRA_KEY_EVENT = "android.intent.extra.KEY_EVENT";

    private String action;
    private Class<?> componentClass;
    private final Map<String, Object> extras = new HashMap<>();

    public Intent() {
    }

    public Intent(String action) {
        this.action = action;
    }

    public Intent(Context context, Class<?> cls) {
        this.componentClass = cls;
    }

    public String getAction() {
        return action;
    }

    public Intent setAction(String action) {
        this.action = action;
        return this;
    }

    public Class<?> getComponentClass() {
        return componentClass;
    }

    public Intent putExtra(String name, boolean value) {
        extras.put(name, Boolean.valueOf(value));
        return this;
    }

    public Intent putExtra(String name, int value) {
        extras.put(name, Integer.valueOf(value));
        return this;
    }

    public Intent putExtra(String name, Object value) {
        extras.put(name, value);
        return this;
    }

    public boolean hasExtra(String name) {
        return extras.containsKey(name);
    }

    public boolean getBooleanExtra(String name, boolean defaultValue) {
        Object value = extras.get(name);
        return value instanceof Boolean ? ((Boolean) value).booleanValue() : defaultValue;
    }

    public int getIntExtra(String name, int defaultValue) {
        Object value = extras.get(name);
        return value instanceof Integer ? ((Integer) value).intValue() : defaultValue;
    }

    @SuppressWarnings("unchecked")
    public <T> T getParcelableExtra(String name, Class<T> cls) {
        Object value = extras.get(name);
        if (cls != null && cls.isInstance(value)) {
            return (T) value;
        }
        return null;
    }

    @SuppressWarnings("unchecked")
    public <T> T getParcelableExtra(String name) {
        return (T) extras.get(name);
    }
}
