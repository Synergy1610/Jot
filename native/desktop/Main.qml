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
        if (backend.ready && !backend.flush())
            close.accepted = false
        else
            Qt.callLater(Qt.quit)
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
                return "ok hidden"
            }
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
                    color: "#b59790"
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: window.headerDate
                }

                Item {
                    id: statusIcons
                    objectName: "statusIcons"
                    Layout.preferredWidth: 88
                    Layout.preferredHeight: 30
                    Layout.alignment: Qt.AlignVCenter

                    property Item hoveredBadge: null
                    property string tipText: ""
                    property bool tipVisible: false
                    property real tooltipLeftEdge: badgeTooltip.x
                    property real tooltipRightEdge: badgeTooltip.x + badgeTooltip.width

                    function showTip(badge, text) {
                        hoveredBadge = badge
                        tipText = text
                        tipDelay.restart()
                        tipHide.stop()
                    }
                    function hideTip() {
                        hoveredBadge = null
                        tipDelay.stop()
                        tipHide.stop()
                        tipVisible = false
                    }

                    Timer {
                        id: tipDelay
                        interval: 220
                        repeat: false
                        onTriggered: {
                            if (statusIcons.hoveredBadge) {
                                statusIcons.tipVisible = true
                                tipHide.restart()
                            }
                        }
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
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36
                        height: 30
                        padding: 0
                        hoverEnabled: true
                        Accessible.name: backend.localTooltip
                        background: null
                        onHoveredChanged: hovered ? statusIcons.showTip(localBadge, backend.localTooltip)
                                                  : statusIcons.hideTip()
                        onActiveFocusChanged: activeFocus ? statusIcons.showTip(localBadge, backend.localTooltip)
                                                          : statusIcons.hideTip()
                        contentItem: Item {
                            Canvas {
                                id: monitorIcon
                                objectName: "monitorIcon"
                                width: 15
                                height: 15
                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = backend.localError ? "#c38b7b"
                                                      : (backend.locallySaved ? "#b59790" : "#584e51")
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
                                anchors.leftMargin: 3
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Label {
                                id: localTick
                                objectName: "localTick"
                                anchors.left: monitorIcon.right
                                anchors.leftMargin: 3
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
                        anchors.left: localBadge.right
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 48
                        height: 30
                        padding: 0
                        hoverEnabled: true
                        Accessible.name: qsTr("Cloud backup is off — sign in to enable")
                        background: null
                        onHoveredChanged: hovered ? statusIcons.showTip(cloudBadge, qsTr("Cloud backup is off — sign in to enable"))
                                                  : statusIcons.hideTip()
                        onActiveFocusChanged: activeFocus ? statusIcons.showTip(cloudBadge, qsTr("Cloud backup is off — sign in to enable"))
                                                          : statusIcons.hideTip()
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
                                    ctx.strokeStyle = "#584e51"
                                    ctx.lineWidth = 1.2
                                    ctx.beginPath()
                                    ctx.moveTo(4, 12)
                                    ctx.bezierCurveTo(0, 12, 0, 7, 3.5, 7)
                                    ctx.bezierCurveTo(4, 2.5, 10, 2, 11.5, 6)
                                    ctx.bezierCurveTo(15.5, 6, 15.5, 12, 12, 12)
                                    ctx.closePath()
                                    ctx.stroke()
                                }
                            }
                            Loader {
                                id: cloudSpinner
                                objectName: "cloudSpinner"
                                sourceComponent: window.spinnerComponent
                                visible: backend.cloudSaving
                                anchors.left: cloudIcon.right
                                anchors.leftMargin: 3
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Label {
                                id: cloudTick
                                objectName: "cloudTick"
                                anchors.left: cloudIcon.right
                                anchors.leftMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                visible: backend.cloudSaved
                                text: "✓"
                                font.pixelSize: 11
                                font.bold: true
                                color: "#b59790"
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
                leftPadding: 16
                rightPadding: 16
                background: null
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        const caret = backend.finishLine(selectionStart, selectionEnd)
                        if (caret >= 0) {
                            cursorPosition = caret
                            event.accepted = true
                        }
                    }
                }
                Component.onCompleted: {
                    backend.attachEditor(textDocument)
                    forceActiveFocus()
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

        contentItem: Flickable {
            id: settingsFlick
            objectName: "settingsFlick"
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
                        text: qsTr("Jot settings")
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
                        onClicked: settings.close()
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
                    Text { objectName: "settingsAccountHeading"; text: qsTr("Account"); font.family: "monospace"; font.pixelSize: 15; font.bold: true; color: "#87a9b0" }
                    Text {
                        objectName: "settingsBody"
                        Layout.fillWidth: true
                        text: qsTr("Signed out — cloud backup is off. Email/password sign-in arrives in v2. Your notes stay on this machine.")
                        color: "#FAFCFB"
                        font.family: "monospace"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        lineHeight: 1.5
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
    }
}
