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
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import app.jot.ui.EditorScreen
import app.jot.ui.JotColors
import app.jot.ui.JotTheme
import app.jot.ui.SettingsScreen

class MainActivity : ComponentActivity() {
    private lateinit var store: JotStore

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        store = JotStore(this)
        setContent { JotTheme { JotApp(store) } }

        lifecycle.addObserver(object : DefaultLifecycleObserver {
            override fun onStart(owner: LifecycleOwner) {
                JotVisibility.visible = true
                store.refreshDate()
                SyncScheduler.cancel(this@MainActivity)
            }

            override fun onStop(owner: LifecycleOwner) {
                // Same rule as the desktop: leaving the app is the sync point.
                JotVisibility.visible = false
                store.syncNow()
                SyncScheduler.schedule(this@MainActivity)
            }
        })
    }
}

@Composable
fun JotApp(store: JotStore) {
    Box(Modifier.fillMaxSize().background(JotColors.ink)) {
        if (store.settingsOpen) SettingsScreen(store) else EditorScreen(store)
    }
}
