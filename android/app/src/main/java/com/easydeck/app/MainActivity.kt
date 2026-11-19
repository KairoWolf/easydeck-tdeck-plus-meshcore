package com.easydeck.app

import android.Manifest
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.*
import com.easydeck.app.ui.ChatScreen
import com.easydeck.app.ui.ChatsScreen
import com.easydeck.app.ui.ConnectScreen
import com.easydeck.app.ui.PeopleScreen
import com.easydeck.app.ui.C
import com.easydeck.app.ui.tsx
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {
    // once Bluetooth permission is granted, reconnect to the last device right away
    private val perms = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        (application as EasyDeckApp).repo.reconnectLast()
    }
    private var openConv by mutableStateOf<String?>(null)

    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        intent.getStringExtra("conv")?.let { openConv = it }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        openConv = intent.getStringExtra("conv")
        perms.launch(arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.POST_NOTIFICATIONS))
        setContent {
            val app = application as EasyDeckApp
            val repo = app.repo
            val state by repo.ble.state.collectAsState()
            val msgs by repo.messages.collectAsState()
            var open by remember { mutableStateOf(openConv) }
            LaunchedEffect(openConv) { if (openConv != null) open = openConv }
            app.notifier.visibleConv = open
            // offline: still show the saved history if we have one
            val haveHistory = msgs.isNotEmpty()
            if (state != BleLink.State.READY && !haveHistory) {
                ConnectScreen(repo)
            } else if (open != null) {
                BackHandler { open = null }
                ChatScreen(repo, open!!) { open = null }
            } else {
                var tab by remember { mutableStateOf(0) }
                Column(Modifier.fillMaxSize().background(C.bg)) {
                    Box(Modifier.weight(1f)) {
                        if (tab == 0) ChatsScreen(repo) { open = it } else PeopleScreen(repo) { open = it }
                    }
                    Row(Modifier.fillMaxWidth().background(C.surface)) {
                        listOf("CHATS", "PEOPLE").forEachIndexed { i, label ->
                            Box(Modifier.weight(1f).clickable { tab = i }.padding(vertical = 14.dp), contentAlignment = Alignment.Center) {
                                Text(label, style = tsx(15, if (tab == i) C.amber else C.muted, true))
                            }
                        }
                    }
                }
            }
        }
    }
}
