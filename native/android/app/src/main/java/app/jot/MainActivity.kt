package app.jot

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.core.view.WindowCompat
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import app.jot.ui.EditorScreen
import app.jot.ui.JotColors
import app.jot.ui.JotTheme
import app.jot.ui.SettingsScreen
import app.jot.ui.SyncDialogs

class MainActivity : ComponentActivity() {
    private lateinit var store: JotStore

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        WindowCompat.getInsetsController(window, window.decorView)?.apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
        }
        store = JotStores.get(applicationContext)
        setContent { JotTheme { JotApp(store) } }

        lifecycle.addObserver(object : DefaultLifecycleObserver {
            override fun onStart(owner: LifecycleOwner) {
                store.refreshDate()
            }

            override fun onStop(owner: LifecycleOwner) {
                store.flushNow()
            }
        })
    }
}

@Composable
fun JotApp(store: JotStore) {
    Box(Modifier.fillMaxSize().background(JotColors.ink)) {
        if (store.settingsOpen) SettingsScreen(store) else EditorScreen(store)
        SyncDialogs(store)
    }
}
