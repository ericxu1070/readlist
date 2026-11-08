import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 780
    height: 560
    minimumWidth: 520
    minimumHeight: 360
    visible: true
    title: qsTr("Reading List")
    color: theme.background
    font.family: "monospace"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.foreground: theme.foreground
    Material.background: theme.background
    Material.roundedScale: theme.radius

    property bool showDone: false
    property bool adding: false
    property bool abstractOpen: false
    property bool helpVisible: false
    property string selectedKey: ""
    property bool selectTopNext: false
    // Which end a held Shift+Up/Down sends the paper to.
    property string holdTarget: "top"

    readonly property var papers: showDone ? backend.done : backend.queue
    readonly property var info: abstractOpen && selectedKey !== ""
                                ? (backend.infoRevision, backend.info(selectedKey)) : ({})

    function indexOfKey(key) {
        for (let i = 0; i < papers.length; ++i)
            if (papers[i].key === key) return i
        return -1
    }

    // The list model is replaced on every change, so keep the selection on
    // the same paper (or the same row, if that paper left this list).
    function restoreSelection() {
        if (selectTopNext && papers.length > 0) {
            selectTopNext = false
            select(0)
            return
        }
        const i = indexOfKey(selectedKey)
        if (i >= 0) select(i)
        else select(Math.min(Math.max(list.currentIndex, 0), papers.length - 1))
    }

    function select(i) {
        if (papers.length === 0) {
            list.currentIndex = -1
            selectedKey = ""
            return
        }
        i = Math.max(0, Math.min(i, papers.length - 1))
        list.currentIndex = i
        selectedKey = papers[i].key
        list.positionViewAtIndex(i, ListView.Contain)
    }

    function openSelected(keepOpen) {
        if (selectedKey === "") return
        if (backend.openPaper(selectedKey) && !keepOpen)
            Qt.quit()
    }

    function startAdding() {
        adding = true
        abstractOpen = false
        helpVisible = false
        addField.text = backend.clipboardLink()
        addField.selectAll()
        addField.forceActiveFocus()
    }

    function stopAdding() {
        adding = false
        list.forceActiveFocus()
    }

    function switchView() {
        showDone = !showDone
        abstractOpen = false
        selectedKey = ""
        Qt.callLater(() => select(0))
    }

    Connections {
        target: backend
        function onListChanged() { Qt.callLater(win.restoreSelection) }
    }

    Component.onCompleted: {
        select(0)
        list.forceActiveFocus()
    }

    Timer {
        id: holdTimer
        interval: 450
        onTriggered: if (win.selectedKey !== "") backend.move(win.selectedKey, win.holdTarget)
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            // Header: the two lists, switched with Tab.
            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                Label {
                    text: qsTr("Reading List")
                    font.pixelSize: 17
                    font.bold: true
                    color: theme.foreground
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Repeater {
                    model: [
                        { done: false, label: qsTr("Queue"), count: backend.queue.length },
                        { done: true, label: qsTr("Read"), count: backend.done.length }
                    ]
                    delegate: Column {
                        required property var modelData
                        readonly property bool active: win.showDone === modelData.done
                        spacing: 4
                        Label {
                            text: modelData.label + "  " + modelData.count.toLocaleString(Qt.locale(), 'f', 0)
                            font.pixelSize: 13
                            color: parent.active ? theme.accent : theme.muted
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (!parent.parent.active) win.switchView()
                            }
                        }
                        Rectangle {
                            width: parent.width
                            height: 2
                            color: parent.active ? theme.accent : "transparent"
                        }
                    }
                }
            }

            TextField {
                id: addField
                visible: win.adding
                Layout.fillWidth: true
                placeholderText: qsTr("arXiv link or id, or any web address")
                font.pixelSize: 14
                onAccepted: {
                    if (text.trim() !== "") {
                        win.selectTopNext = true
                        backend.add(text)
                    }
                    win.stopAdding()
                }
                Keys.onEscapePressed: win.stopAdding()
            }

            // The list and the abstract view share this space.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    model: win.papers
                    currentIndex: -1
                    boundsBehavior: Flickable.StopAtBounds
                    highlightMoveDuration: 0
                    visible: !win.abstractOpen
                    focus: true
                    ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                    delegate: Item {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool selected: index === list.currentIndex
                        width: ListView.view.width
                        height: 58

                        Rectangle {
                            anchors.fill: parent
                            anchors.rightMargin: 10
                            radius: Math.min(6, theme.radius)
                            color: row.selected ? theme.selection : "transparent"
                        }
                        Rectangle {
                            visible: row.selected
                            width: 3
                            height: parent.height - 16
                            anchors.verticalCenter: parent.verticalCenter
                            color: theme.accent
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 22
                            spacing: 12
                            Label {
                                visible: !win.showDone
                                text: (row.index + 1).toLocaleString(Qt.locale(), 'f', 0)
                                color: row.selected ? theme.foreground : theme.muted
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 24
                            }
                            Label {
                                visible: win.showDone
                                text: "✓"
                                color: theme.accent
                                font.pixelSize: 13
                                Layout.preferredWidth: 24
                                horizontalAlignment: Text.AlignRight
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    elide: Text.ElideRight
                                    font.pixelSize: 15
                                    color: theme.foreground
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.pending ? qsTr("Fetching the title…")
                                          : win.showDone && row.modelData.read !== ""
                                            ? row.modelData.subtitle + "  ·  " + qsTr("read %1").arg(row.modelData.read)
                                            : row.modelData.subtitle
                                    elide: Text.ElideRight
                                    font.pixelSize: 12
                                    // Muted text is blended for the window
                                    // background and fades on the selection.
                                    color: theme.muted
                                    Binding on color { when: row.selected; value: Qt.rgba(Qt.color(theme.foreground).r, Qt.color(theme.foreground).g, Qt.color(theme.foreground).b, 0.75) }
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: win.select(row.index)
                            onDoubleClicked: { win.select(row.index); win.openSelected(false) }
                        }
                    }

                    Keys.onPressed: (event) => win.handleKey(event)
                    Keys.onReleased: (event) => {
                        const vertical = event.key === Qt.Key_Up || event.key === Qt.Key_Down
                                         || event.key === Qt.Key_K || event.key === Qt.Key_J
                        if (vertical && !event.isAutoRepeat) holdTimer.stop()
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: win.papers.length === 0 && !win.abstractOpen
                    text: win.showDone ? qsTr("Nothing read yet.") : qsTr("Nothing queued. Press A to add a paper.")
                    color: theme.muted
                    font.pixelSize: 14
                }

                // The abstract view: Up and Down keep moving through the list,
                // so abstracts can be skimmed one after another.
                Flickable {
                    id: abstractView
                    anchors.fill: parent
                    visible: win.abstractOpen
                    clip: true
                    contentWidth: width
                    contentHeight: abstractColumn.implicitHeight + 8
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: abstractView.contentHeight > abstractView.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                    ColumnLayout {
                        id: abstractColumn
                        y: 6
                        width: abstractView.width - 16
                        spacing: 10
                        Label {
                            Layout.fillWidth: true
                            text: win.info.title || ""
                            wrapMode: Text.Wrap
                            font.pixelSize: 18
                            font.bold: true
                            color: theme.foreground
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: win.info.authors || ""
                            wrapMode: Text.Wrap
                            font.pixelSize: 13
                            color: theme.accent
                        }
                        Label {
                            Layout.fillWidth: true
                            text: [qsTr("%1 of %2").arg(list.currentIndex + 1).arg(win.papers.length),
                                   win.info.published || "",
                                   win.info.arxivId ? "arXiv " + win.info.arxivId : (win.info.url || "")]
                                  .filter(s => s !== "").join("  ·  ")
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            color: theme.muted
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: theme.selection }
                        Label {
                            Layout.fillWidth: true
                            text: win.info.loading ? qsTr("Fetching the abstract…")
                                  : win.info.error ? qsTr("Could not fetch the abstract: %1").arg(win.info.error)
                                  : (win.info.abstract || qsTr("No abstract available."))
                            wrapMode: Text.Wrap
                            font.pixelSize: 14
                            lineHeight: 1.25
                            color: win.info.error ? theme.error : theme.foreground
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: backend.status !== ""
                text: backend.status
                elide: Text.ElideRight
                font.pixelSize: 12
                color: backend.statusIsError ? theme.error : theme.accent
            }

            Label {
                Layout.fillWidth: true
                text: win.abstractOpen
                      ? qsTr("Enter open  ·  ↑↓ next  ·  PgUp/PgDn scroll  ·  Space/Esc back")
                      : win.showDone
                        ? qsTr("Enter open  ·  Space abstract  ·  U unread  ·  Tab queue  ·  ? keys")
                        : qsTr("Enter open  ·  Space abstract  ·  R read  ·  A add  ·  Shift+↑↓ move (hold: top/bottom)  ·  ? keys")
                elide: Text.ElideRight
                font.pixelSize: 12
                color: theme.muted
            }
        }

        // Every shortcut, shown with "?".
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.6)
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 560)
                height: helpGrid.implicitHeight + 48
                radius: theme.radius
                color: theme.background
                border.color: theme.accent
                border.width: 1

                Column {
                    id: helpGrid
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
                    spacing: 6
                    Repeater {
                        model: [
                            ["Enter", qsTr("Open the paper and close")],
                            ["Ctrl+Enter", qsTr("Open the paper, stay open")],
                            ["Space", qsTr("Show or hide the abstract")],
                            ["↑ ↓  /  K J", qsTr("Select")],
                            ["Shift+↑ ↓", qsTr("Move one place")],
                            [qsTr("Hold Shift+↑ ↓"), qsTr("Move to the top or bottom")],
                            ["R", qsTr("Mark read (in Read: unread)")],
                            ["A", qsTr("Add a paper (link from clipboard)")],
                            ["D / Delete", qsTr("Remove")],
                            ["Tab", qsTr("Switch between Queue and Read")],
                            ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo")],
                            ["Esc / Q", qsTr("Close")]
                        ]
                        delegate: Row {
                            required property var modelData
                            spacing: 16
                            Label {
                                width: 200
                                text: modelData[0]
                                color: theme.accent
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Label {
                                width: helpGrid.width - 216
                                text: modelData[1]
                                color: theme.foreground
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    function handleKey(event) {
        const shift = event.modifiers & Qt.ShiftModifier
        const ctrl = event.modifiers & Qt.ControlModifier
        const key = event.key
        event.accepted = true

        if (helpVisible) {
            helpVisible = false
            return
        }

        const up = key === Qt.Key_Up || key === Qt.Key_K
        const down = key === Qt.Key_Down || key === Qt.Key_J

        // A tap moves the paper one place; holding past the timer sends it to
        // the end, and the auto-repeats in between are ignored.
        if (shift && (up || down) && !ctrl) {
            if (showDone || selectedKey === "" || abstractOpen) return
            if (!event.isAutoRepeat) {
                holdTarget = up ? "top" : "bottom"
                backend.move(selectedKey, up ? "up" : "down")
                holdTimer.restart()
            }
            return
        }
        if (up) { select(list.currentIndex - 1); return }
        if (down) { select(list.currentIndex + 1); return }

        if (abstractOpen && (key === Qt.Key_PageDown || key === Qt.Key_PageUp)) {
            const step = abstractView.height * 0.8 * (key === Qt.Key_PageDown ? 1 : -1)
            abstractView.contentY = Math.max(0, Math.min(abstractView.contentY + step,
                                                         abstractView.contentHeight - abstractView.height))
            return
        }

        switch (key) {
        case Qt.Key_Home:
            if (shift && !showDone && selectedKey !== "") backend.move(selectedKey, "top")
            else select(0)
            return
        case Qt.Key_End:
            if (shift && !showDone && selectedKey !== "") backend.move(selectedKey, "bottom")
            else select(papers.length - 1)
            return
        case Qt.Key_PageDown: select(list.currentIndex + 8); return
        case Qt.Key_PageUp: select(list.currentIndex - 8); return
        case Qt.Key_Return:
        case Qt.Key_Enter:
            openSelected(ctrl)
            return
        case Qt.Key_Space:
            if (selectedKey !== "") abstractOpen = !abstractOpen
            abstractView.contentY = 0
            return
        case Qt.Key_Tab:
        case Qt.Key_Backtab:
            switchView()
            return
        case Qt.Key_Escape:
            if (abstractOpen) abstractOpen = false
            else Qt.quit()
            return
        case Qt.Key_Q:
            Qt.quit()
            return
        case Qt.Key_Question:
            helpVisible = true
            return
        case Qt.Key_Z:
            if (ctrl && shift) backend.redo()
            else if (ctrl) backend.undo()
            return
        case Qt.Key_Y:
            if (ctrl) backend.redo()
            return
        }
        if (ctrl) { event.accepted = false; return }

        switch (key) {
        case Qt.Key_A:
        case Qt.Key_N:
            startAdding()
            return
        case Qt.Key_R:
        case Qt.Key_X:
            if (selectedKey === "") return
            if (showDone) backend.markUnread(selectedKey)
            else backend.markRead(selectedKey)
            return
        case Qt.Key_U:
            if (showDone && selectedKey !== "") backend.markUnread(selectedKey)
            return
        case Qt.Key_D:
        case Qt.Key_Delete:
            if (selectedKey !== "") backend.remove(selectedKey)
            return
        }
        event.accepted = false
    }
}
