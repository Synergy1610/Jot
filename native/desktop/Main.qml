import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 480
    height: 320
    // The installed pad is non-resizable. Wayland size hints also let
    // Hyprland identify the window as floating before its first map.
    minimumWidth: 480
    maximumWidth: 480
    minimumHeight: 320
    maximumHeight: 320
    visible: !backend.startHidden
    title: backend.appTitle
    color: "#000000"
    Material.theme: Material.Dark
    Material.accent: "#b59790"

    property string headerDate: backend.displayDate()
    signal summoned()

    property Component spinnerComponent: Component {
        Item {
            id: spinner
            property real angle: 0
            implicitWidth: 12
            implicitHeight: 12
            Canvas {
                id: arc
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = "#b59790"
                    ctx.lineWidth = 1.4
                    ctx.lineCap = "round"
                    const from = spinner.angle * Math.PI / 180
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, Math.min(width, height) / 2 - 1.5,
                            from, from + 250 * Math.PI / 180)
                    ctx.stroke()
                }
                Component.onCompleted: requestPaint()
            }
            Timer {
                running: spinner.visible
                interval: 70
                repeat: true
                onTriggered: {
                    spinner.angle = (spinner.angle + 30) % 360
                    arc.requestPaint()
                }
            }
        }
    }

    Timer {
        interval: 60000
        running: true
        repeat: true
        onTriggered: window.headerDate = backend.displayDate()
    }
    onClosing: function(close) {
        if (backend.ready && !backend.flush()) {
            close.accepted = false
            return
        }
        // Let the window close immediately, then finish the cloud backup
        // while the process exits invisibly.
        close.accepted = true
        if (backend.ready)
            backend.quitAfterSync()
        else
            Qt.callLater(Qt.quit)
    }

    // Keep the caret line fully visible. Qt's TextArea does not reliably
    // follow the caret when the content grows, which leaves half a line cut
    // off at the bottom of the window.
    function ensureCaretVisible() {
        const flick = editorScroll.contentItem
        if (!flick || !editor || flick.contentY === undefined) return
        const margin = 8
        const caret = editor.cursorRectangle
        const caretTop = caret.y
        const caretBottom = caret.y + caret.height
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        if (caretBottom > flick.contentY + flick.height - margin)
            flick.contentY = Math.min(maxY, caretBottom - flick.height + margin)
        else if (caretTop < flick.contentY + margin)
            flick.contentY = Math.max(0, caretTop - margin)
    }

    function handleCommand(command) {
        if (command === "toggle")
            command = visible ? "hide" : "show"
        if (command === "show") {
            show()
            raise()
            requestActivate()
            editor.cursorPosition = editor.length
            editor.forceActiveFocus()
            summoned()
            return "ok visible"
        }
        if (command === "settings") {
            handleCommand("show")
            settings.open()
            return "ok settings"
        }
        if (command === "hide" || command === "quit") {
            if (backend.ready && !backend.flush()) {
                if (!visible)
                    handleCommand("show")
                return "error " + backend.status
            }
            if (command === "hide") {
                hide()
                if (backend.ready)
                    backend.syncAfterHide()
                return "ok hidden"
            }
            // Quitting hides the window immediately and lets the backup finish
            // before the process exits.
            hide()
            if (backend.ready)
                backend.quitAfterSync()
            else
                Qt.callLater(Qt.quit)
            return "ok quitting"
        }
        return "error unknown command"
    }

    Shortcut {
        sequence: "Escape"
        enabled: !settings.visible
        onActivated: window.handleCommand("hide")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            z: 2
            color: "#090809"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 82
                    color: "#b59790"
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: window.headerDate
                }

                Item {
                    id: statusIcons
                    objectName: "statusIcons"
                    Layout.preferredWidth: 61
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter

                    property Item hoveredBadge: null
                    property string tipText: ""
                    property bool tipVisible: false
                    property real tooltipLeftEdge: badgeTooltip.x
                    property real tooltipRightEdge: badgeTooltip.x + badgeTooltip.width

                    function showTip(badge, text) {
                        if (!text) text = qsTr("Cloud backup")
                        hoveredBadge = badge
                        tipText = text
                        tipVisible = true
                        tipHide.restart()
                    }
                    function hideTip(badge) {
                        // A leave event from the previously hovered badge must
                        // not clear a tooltip that another badge just showed.
                        if (badge && hoveredBadge !== badge) return
                        hoveredBadge = null
                        tipHide.stop()
                        tipVisible = false
                    }

                    Timer {
                        id: tipHide
                        interval: 4000
                        repeat: false
                        onTriggered: statusIcons.tipVisible = false
                    }

                    ToolButton {
                        id: localBadge
                        objectName: "localBadge"
                        anchors.left: cloudBadge.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 30
                        padding: 0
                        hoverEnabled: true
                        Accessible.name: backend.localTooltip
                        background: null
                        onHoveredChanged: hovered ? statusIcons.showTip(localBadge, backend.localTooltip)
                                                  : statusIcons.hideTip(localBadge)
                        onActiveFocusChanged: activeFocus ? statusIcons.showTip(localBadge, backend.localTooltip)
                                                          : statusIcons.hideTip(localBadge)
                        contentItem: Item {
                            Canvas {
                                id: monitorIcon
                                objectName: "monitorIcon"
                                width: 15
                                height: 15
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = backend.localError ? "#c38b7b"
                                                      : (backend.ready ? "#b59790" : "#584e51")
                                    ctx.lineWidth = 1.2
                                    ctx.strokeRect(1.5, 2.5, 12, 8)
                                    ctx.beginPath()
                                    ctx.moveTo(7.5, 10.5)
                                    ctx.lineTo(7.5, 13)
                                    ctx.moveTo(4.5, 13)
                                    ctx.lineTo(10.5, 13)
                                    ctx.stroke()
                                }
                                Connections {
                                    target: backend
                                    function onStatusChanged() { monitorIcon.requestPaint() }
                                }
                            }
                            Loader {
                                id: localSpinner
                                objectName: "localSpinner"
                                sourceComponent: window.spinnerComponent
                                visible: backend.saving
                                anchors.left: monitorIcon.right
                                anchors.leftMargin: 5
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Label {
                                id: localTick
                                objectName: "localTick"
                                anchors.left: monitorIcon.right
                                anchors.leftMargin: 5
                                anchors.verticalCenter: parent.verticalCenter
                                visible: backend.locallySaved
                                text: "✓"
                                font.pixelSize: 11
                                font.bold: true
                                color: "#b59790"
                            }
                        }
                    }

                    ToolButton {
                        id: cloudBadge
                        objectName: "cloudBadge"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 21
                        height: 30
                        padding: 0
                        hoverEnabled: true
                        Accessible.name: backend.cloudTooltip
                        background: null
                        onHoveredChanged: hovered ? statusIcons.showTip(cloudBadge, backend.cloudTooltip)
                                                  : statusIcons.hideTip(cloudBadge)
                        onActiveFocusChanged: activeFocus ? statusIcons.showTip(cloudBadge, backend.cloudTooltip)
                                                          : statusIcons.hideTip(cloudBadge)
                        contentItem: Item {
                            Canvas {
                                id: cloudIcon
                                objectName: "cloudIcon"
                                width: 15
                                height: 15
                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = backend.cloudSignedIn ? "#b59790" : "#584e51"
                                    ctx.lineWidth = 1.2
                                    ctx.beginPath()
                                    ctx.moveTo(4, 12)
                                    ctx.bezierCurveTo(0, 12, 0, 7, 3.5, 7)
                                    ctx.bezierCurveTo(4, 2.5, 10, 2, 11.5, 6)
                                    ctx.bezierCurveTo(15.5, 6, 15.5, 12, 12, 12)
                                    ctx.closePath()
                                    ctx.stroke()
                                }
                                Connections {
                                    target: backend
                                    function onCloudChanged() { cloudIcon.requestPaint() }
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: badgeTooltip
                        objectName: "badgeTooltip"
                        parent: window.contentItem
                        visible: statusIcons.tipVisible && statusIcons.hoveredBadge !== null
                        z: 100
                        x: {
                            if (!statusIcons.hoveredBadge) return 0
                            const origin = statusIcons.mapToItem(window.contentItem, 0, 0)
                            const desired = origin.x + statusIcons.hoveredBadge.x
                                + statusIcons.hoveredBadge.width / 2 - width / 2
                            return Math.max(0, Math.min(window.width - width, desired))
                        }
                        y: {
                            const origin = statusIcons.mapToItem(window.contentItem, 0, 0)
                            return origin.y + statusIcons.height + 2
                        }
                        width: tipLabel.implicitWidth + 18
                        height: tipLabel.implicitHeight + 10
                        radius: 6
                        color: "#000000"
                        opacity: 1
                        border.color: "#584e51"
                        border.width: 1
                        Text {
                            id: tipLabel
                            objectName: "badgeTooltipText"
                            x: 9
                            y: 5
                            text: statusIcons.tipText
                            color: "#FAFCFB"
                            font.family: "monospace"
                            font.pixelSize: 13
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 9
                }

                ToolButton {
                    id: gearButton
                    objectName: "openSettings"
                    implicitWidth: 24
                    implicitHeight: 28
                    padding: 0
                    hoverEnabled: true
                    Accessible.name: qsTr("Settings")
                    background: null
                    contentItem: Item {
                        Image {
                            id: gearIcon
                            objectName: "gearIcon"
                            width: 17
                            height: 17
                            anchors.centerIn: parent
                            source: "qrc:/gear.svg"
                            sourceSize: Qt.size(17, 17)
                            opacity: 1
                        }
                    }
                    onClicked: settings.open()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: "#584e51"
            }
        }

        ScrollView {
            id: editorScroll
            objectName: "editorScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                id: editor
                objectName: "padEditor"
                enabled: backend.ready
                text: backend.content
                color: "#FAFCFB"
                selectionColor: "#584e51"
                selectedTextColor: "#FAFCFB"
                font.family: "monospace"
                font.pixelSize: 13
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("Start typing…")
                topPadding: 10
                bottomPadding: 12
                leftPadding: 16
                rightPadding: 16
                background: null
                onCursorRectangleChanged: window.ensureCaretVisible()
                onContentHeightChanged: window.ensureCaretVisible()
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        const caret = backend.finishLine(selectionStart, selectionEnd)
                        if (caret >= 0) {
                            cursorPosition = caret
                            event.accepted = true
                            Qt.callLater(window.ensureCaretVisible)
                        }
                    }
                }
                Component.onCompleted: {
                    backend.attachEditor(textDocument)
                    forceActiveFocus()
                    Qt.callLater(window.ensureCaretVisible)
                }
            }
        }

        Button {
            objectName: "copyOriginalButton"
            visible: backend.incompatibleText
            Layout.leftMargin: 16
            Layout.bottomMargin: 6
            text: qsTr("Copy original notes")
            onClicked: backend.copyOriginal()
        }
    }

    Popup {
        id: settings
        objectName: "settingsPopup"
        parent: Overlay.overlay
        x: 0
        y: 0
        width: window.width
        height: window.height
        modal: true
        focus: true
        padding: 16
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: "#090809" }

        property bool confirmClear: false
        property bool copied: false
        property bool helpVisible: false
        onHelpVisibleChanged: {
            settingsFlick.contentY = 0
            helpFlick.contentY = 0
        }
        property string hotkeyStatus: backend.hotkey.status === qsTr("Looks configured")
            ? qsTr("Looks configured")
            : (backend.hotkey.status === qsTr("Not found") ? qsTr("Not found") : qsTr("Checking config…"))

        Timer {
            id: copiedTimer
            interval: 1500
            repeat: false
            onTriggered: settings.copied = false
        }
        onAboutToShow: {
            confirmClear = false
            copied = false
            backend.hotkey.refresh()
        }
        onClosed: editor.forceActiveFocus()

        contentItem: Item {
        Flickable {
            id: settingsFlick
            objectName: "settingsFlick"
            visible: !settings.helpVisible
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: settingsColumn.implicitHeight

            ColumnLayout {
                id: settingsColumn
                width: settingsFlick.width
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 12
                    Text {
                        objectName: "settingsTitle"
                        text: settings.helpVisible ? qsTr("Jot help") : qsTr("Jot settings")
                        font.family: "monospace"
                        font.pixelSize: 17
                        font.bold: true
                        color: "#FAFCFB"
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        id: closeButton
                        objectName: "settingsClose"
                        implicitWidth: 30
                        implicitHeight: 34
                        Layout.alignment: Qt.AlignVCenter
                        padding: 0
                        background: null
                        onClicked: settings.helpVisible ? settings.helpVisible = false : settings.close()
                        contentItem: Text {
                            text: "✕"
                            font.family: "monospace"
                            font.pixelSize: 17
                            color: closeButton.hovered ? "#b59790" : "#584e51"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#87a9b0"
                    Layout.bottomMargin: 12
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Layout.bottomMargin: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            objectName: "settingsAccountHeading"
                            text: qsTr("Account")
                            font.family: "monospace"
                            font.pixelSize: 15
                            font.bold: true
                            color: "#87a9b0"
                            Layout.fillWidth: true
                        }
                        ToolButton {
                            id: helpButton
                            objectName: "settingsHelp"
                            implicitWidth: 28
                            implicitHeight: 28
                            Layout.alignment: Qt.AlignVCenter
                            padding: 0
                            hoverEnabled: true
                            background: null
                            Accessible.name: qsTr("Help")
                            onClicked: settings.helpVisible = true
                            contentItem: Text {
                                text: "?"
                                font.family: "monospace"
                                font.pixelSize: 15
                                font.bold: true
                                color: helpButton.hovered ? "#b59790" : "#584e51"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                    Text {
                        objectName: "settingsBody"
                        Layout.fillWidth: true
                        text: backend.cloudSignedIn
                            ? qsTr("Signed in as %1. Notes are backed up when you minimise or close Jot.")
                                .arg(backend.cloudAccount)
                            : qsTr("Signed out — notes stay on this machine until you sign in.")
                        color: "#FAFCFB"
                        font.family: "monospace"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        lineHeight: 1.5
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Email")
                            color: "#b59790"
                            font.family: "monospace"
                            font.pixelSize: 13
                            Layout.preferredWidth: 60
                        }
                        TextField {
                            id: cloudEmail
                            objectName: "cloudEmail"
                            Layout.fillWidth: true
                            color: "#FAFCFB"
                            font.family: "monospace"
                            font.pixelSize: 13
                            leftPadding: 8
                            rightPadding: 8
                            implicitHeight: 30
                            background: Rectangle {
                                color: "#000000"
                                radius: 6
                                border.width: 1
                                border.color: "#584e51"
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Password")
                            color: "#b59790"
                            font.family: "monospace"
                            font.pixelSize: 13
                            Layout.preferredWidth: 60
                        }
                        TextField {
                            id: cloudPassword
                            objectName: "cloudPassword"
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            color: "#FAFCFB"
                            font.family: "monospace"
                            font.pixelSize: 13
                            leftPadding: 8
                            rightPadding: 8
                            implicitHeight: 30
                            background: Rectangle {
                                color: "#000000"
                                radius: 6
                                border.width: 1
                                border.color: "#584e51"
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            id: signInButton
                            objectName: "cloudSignIn"
                            visible: !backend.cloudSignedIn
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
                            enabled: !backend.cloudSaving && cloudEmail.text.length > 0 && cloudPassword.text.length > 0
                            background: Rectangle {
                                radius: 6; color: "#000000"; border.width: 1
                                border.color: signInButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Sign in")
                                color: signInButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"; font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: backend.cloudSignIn(cloudEmail.text, cloudPassword.text)
                        }
                        Button {
                            id: signUpButton
                            objectName: "cloudSignUp"
                            visible: !backend.cloudSignedIn
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
                            enabled: !backend.cloudSaving && cloudEmail.text.length > 0 && cloudPassword.text.length > 0
                            background: Rectangle {
                                radius: 6; color: "#000000"; border.width: 1
                                border.color: signUpButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Sign up")
                                color: signUpButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"; font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: backend.cloudSignUp(cloudEmail.text, cloudPassword.text)
                        }
                        Button {
                            id: signOutButton
                            objectName: "cloudSignOut"
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
                            visible: backend.cloudSignedIn
                            background: Rectangle {
                                radius: 6; color: "#000000"; border.width: 1
                                border.color: signOutButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Sign out")
                                color: signOutButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"; font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: backend.cloudSignOut()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: backend.cloudSignedIn
                        Button {
                            id: syncButton
                            objectName: "cloudSync"
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
                            enabled: !backend.cloudSaving
                            background: Rectangle {
                                radius: 6; color: "#000000"; border.width: 1
                                border.color: syncButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Back up now")
                                color: syncButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"; font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: backend.cloudSyncNow()
                        }
                        Button {
                            id: pullButton
                            objectName: "cloudPull"
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
                            enabled: !backend.cloudSaving
                            background: Rectangle {
                                radius: 6; color: "#000000"; border.width: 1
                                border.color: pullButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Download")
                                color: pullButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"; font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: backend.cloudPull()
                        }
                    }
                    Text {
                        objectName: "cloudStatus"
                        Layout.fillWidth: true
                        text: backend.cloudStatus
                        color: "#87a9b0"
                        font.family: "monospace"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        Layout.bottomMargin: 12
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#584e51"
                    Layout.bottomMargin: 12
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Layout.bottomMargin: 12
                    Text { text: qsTr("Hotkey (Linux / Hyprland)"); font.family: "monospace"; font.pixelSize: 15; font.bold: true; color: "#87a9b0" }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The keybind belongs to the compositor, not the app. Add this line to your bindings file, then reload Hyprland.")
                        color: "#FAFCFB"
                        font.family: "monospace"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        lineHeight: 1.5
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        color: "#000000"
                        implicitHeight: codeText.implicitHeight + 16
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 2
                            color: "#87a9b0"
                        }
                        Text {
                            id: codeText
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: backend.hotkey.snippet
                            color: "#87a9b0"
                            font.family: "monospace"
                            font.pixelSize: 13
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Layout.topMargin: 4
                        Button {
                            id: copyButton
                            objectName: "copySnippetButton"
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 4
                            bottomPadding: 4
                            background: Rectangle {
                                radius: 6
                                color: "#000000"
                                border.width: 1
                                border.color: copyButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: settings.copied ? qsTr("Copied") : qsTr("Copy")
                                color: copyButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                settings.copied = backend.hotkey.copy()
                                if (settings.copied) copiedTimer.restart()
                            }
                        }
                        Button {
                            id: openConfigButton
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 4
                            bottomPadding: 4
                            background: Rectangle {
                                radius: 6
                                color: "#000000"
                                border.width: 1
                                border.color: openConfigButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Open config")
                                color: openConfigButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: backend.hotkey.openConfig()
                        }
                    }
                    Text {
                        Layout.topMargin: 6
                        text: settings.hotkeyStatus
                        color: settings.hotkeyStatus === qsTr("Looks configured") ? "#87a9b0" : "#c38b7b"
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                    Text {
                        Layout.fillWidth: true
                        text: backend.hotkey.configPath
                        color: "#FAFCFB"
                        font.family: "monospace"
                        font.pixelSize: 13
                        wrapMode: Text.WrapAnywhere
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#584e51"
                    Layout.bottomMargin: 12
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Layout.bottomMargin: 12
                    Text { text: qsTr("Clear pad"); font.family: "monospace"; font.pixelSize: 15; font.bold: true; color: "#87a9b0" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            id: clearButton
                            objectName: "clearButton"
                            visible: !settings.confirmClear
                            enabled: backend.ready && backend.hasContent
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 4
                            bottomPadding: 4
                            background: Rectangle {
                                radius: 6
                                color: "#000000"
                                border.width: 1
                                border.color: clearButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Clear…")
                                color: clearButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: settings.confirmClear = true
                        }
                        Button {
                            id: undoClearButton
                            objectName: "undoClearButton"
                            visible: !settings.confirmClear && backend.canUndoClear
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 4
                            bottomPadding: 4
                            background: Rectangle {
                                radius: 6
                                color: "#000000"
                                border.width: 1
                                border.color: undoClearButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Undo clear")
                                color: undoClearButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: if (backend.undoClear()) settings.close()
                        }
                        Text {
                            visible: settings.confirmClear
                            text: qsTr("Erase everything?")
                            color: "#c38b7b"
                            font.family: "monospace"
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }
                        Button {
                            id: cancelClearButton
                            objectName: "cancelClearButton"
                            visible: settings.confirmClear
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 4
                            bottomPadding: 4
                            background: Rectangle {
                                radius: 6
                                color: "#000000"
                                border.width: 1
                                border.color: cancelClearButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Cancel")
                                color: cancelClearButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: settings.confirmClear = false
                        }
                        Button {
                            id: confirmClearButton
                            objectName: "confirmClearButton"
                            visible: settings.confirmClear
                            implicitHeight: 34
                            Layout.alignment: Qt.AlignVCenter
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 4
                            bottomPadding: 4
                            background: Rectangle {
                                radius: 6
                                color: "#000000"
                                border.width: 1
                                border.color: confirmClearButton.hovered ? "#87a9b0" : "#b59790"
                            }
                            contentItem: Text {
                                text: qsTr("Confirm")
                                color: confirmClearButton.hovered ? "#87a9b0" : "#FAFCFB"
                                font.family: "monospace"
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: if (backend.clearPad()) settings.close()
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#584e51"
                    Layout.bottomMargin: 12
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: qsTr("About"); font.family: "monospace"; font.pixelSize: 15; font.bold: true; color: "#87a9b0" }
                    Text { text: qsTr("version 0.1.0"); color: "#FAFCFB"; font.family: "monospace"; font.pixelSize: 13; wrapMode: Text.WrapAnywhere }
                    Text { text: backend.databasePath; color: "#FAFCFB"; font.family: "monospace"; font.pixelSize: 13; wrapMode: Text.WrapAnywhere }
                    Text { text: qsTr("Hotkey: Super + W"); color: "#FAFCFB"; font.family: "monospace"; font.pixelSize: 13 }
                }
            }
        }
        Flickable {
            id: helpFlick
            objectName: "helpFlick"
            visible: settings.helpVisible
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: helpColumn.implicitHeight
            ColumnLayout {
                id: helpColumn
                width: helpFlick.width
                spacing: 12
                Button {
                    id: helpBack
                    objectName: "helpBack"
                    implicitHeight: 34
                    Layout.alignment: Qt.AlignLeft
                    leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
                    background: Rectangle {
                        radius: 6
                        color: "#000000"
                        border.width: 1
                        border.color: helpBack.hovered ? "#87a9b0" : "#b59790"
                    }
                    contentItem: Text {
                        text: qsTr("← Back to settings")
                        color: helpBack.hovered ? "#87a9b0" : "#FAFCFB"
                        font.family: "monospace"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: settings.helpVisible = false
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("1. Sign up first")
                    color: "#87a9b0"
                    font.family: "monospace"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Create an account with your email and password. If your project requires confirmation, confirm the email, then come back to Jot.")
                    color: "#FAFCFB"
                    font.family: "monospace"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.5
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("2. Sign in")
                    color: "#87a9b0"
                    font.family: "monospace"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("After signing up, use Sign in with the same email and password. Your notes stay local while signed out.")
                    color: "#FAFCFB"
                    font.family: "monospace"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.5
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("3. Automatic backup")
                    color: "#87a9b0"
                    font.family: "monospace"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("While signed in, each local save is backed up to the cloud. The cloud icon spins while uploading and shows a tick when the backup is done.")
                    color: "#FAFCFB"
                    font.family: "monospace"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.5
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("4. Manual sync")
                    color: "#87a9b0"
                    font.family: "monospace"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Back up now pushes your current pad. Download pulls the cloud pad down, but refuses to overwrite unsaved local edits. If the cloud pad changed elsewhere, download it first.")
                    color: "#FAFCFB"
                    font.family: "monospace"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.5
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("5. Sign out")
                    color: "#87a9b0"
                    font.family: "monospace"
                    font.pixelSize: 15
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Sign out stops cloud backup and removes your session from the keyring. Your notes remain on this device.")
                    color: "#FAFCFB"
                    font.family: "monospace"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    lineHeight: 1.5
                }
            }
        }
    }
    }
}
