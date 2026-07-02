package yo6say.latry;

import android.content.SharedPreferences;
import java.util.HashMap;
import java.util.Map;

final class FakeSharedPreferences implements SharedPreferences {
    private final Map<String, Object> values = new HashMap<>();

    @Override
    public boolean getBoolean(String key, boolean defValue) {
        Object value = values.get(key);
        return value instanceof Boolean ? ((Boolean) value).booleanValue() : defValue;
    }

    @Override
    public String getString(String key, String defValue) {
        Object value = values.get(key);
        return value instanceof String ? (String) value : defValue;
    }

    @Override
    public int getInt(String key, int defValue) {
        Object value = values.get(key);
        return value instanceof Integer ? ((Integer) value).intValue() : defValue;
    }

    @Override
    public Editor edit() {
        return new EditorImpl();
    }

    private final class EditorImpl implements SharedPreferences.Editor {
        private final Map<String, Object> pending = new HashMap<>();

        @Override
        public SharedPreferences.Editor putBoolean(String key, boolean value) {
            pending.put(key, Boolean.valueOf(value));
            return this;
        }

        @Override
        public SharedPreferences.Editor putString(String key, String value) {
            pending.put(key, value);
            return this;
        }

        @Override
        public SharedPreferences.Editor putInt(String key, int value) {
            pending.put(key, Integer.valueOf(value));
            return this;
        }

        @Override
        public void apply() {
            values.putAll(pending);
        }
    }
}
