package yo6say.latry;

import org.qtproject.qt.android.bindings.QtApplication;

public final class LatryApplication extends QtApplication {
    @Override
    public void onCreate() {
        super.onCreate();
        LatrySentry.init(this);
    }
}
