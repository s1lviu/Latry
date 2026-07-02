package android.os;

public final class Build {
    public static String BRAND = "";
    public static String MANUFACTURER = "";
    public static String MODEL = "";
    public static String DEVICE = "";
    public static String PRODUCT = "";

    private Build() {
    }

    public static final class VERSION {
        public static int SDK_INT = VERSION_CODES.TIRAMISU;

        private VERSION() {
        }
    }

    public static final class VERSION_CODES {
        public static final int TIRAMISU = 33;
        public static final int UPSIDE_DOWN_CAKE = 34;
        public static final int VANILLA_ICE_CREAM = 35;
        public static final int BAKLAVA = 36;
        public static final int O = 26;

        private VERSION_CODES() {
        }
    }
}
