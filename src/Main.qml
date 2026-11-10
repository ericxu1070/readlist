import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 780
    height: 560
    minimumWidth: 520
    minimumHeight: 360
    visible: true
    title: qsTr("Reading List")
    color: pal.bg
    font.family: pal.family

    Material.theme: pal.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.foreground: theme.foreground
    Material.background: theme.background
    Material.roundedScale: theme.radius

    property bool showDone: false
    property bool adding: false
    property bool searching: false
    property string query: ""
    property bool abstractOpen: false
    property bool helpVisible: false
    property string selectedKey: ""
    property bool selectTopNext: false
    // Which end a held Shift+Up/Down sends the paper to.
    property string holdTarget: "top"

    // pos is the paper's place in the full list, so a filtered row keeps its number.
    readonly property var papers: {
        const words = query.toLowerCase().split(/\s+/).filter(w => w !== "")
        return (showDone ? backend.done : backend.queue)
            .map((p, i) => Object.assign({ pos: i }, p))
            .filter(p => words.every(w => (p.title + " " + p.subtitle).toLowerCase().includes(w)))
    }
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

    function startSearch() {
        searching = true
        abstractOpen = false
        helpVisible = false
        searchField.forceActiveFocus()
    }

    function stopSearch() {
        searching = false
        searchField.clear()
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

    // The theme's colors under Apple's role names (the roles keep their name,
    // the values follow the Omarchy theme).
    QtObject {
        id: pal
        function a(col, alpha) { return Qt.rgba(col.r, col.g, col.b, alpha) }

        readonly property string family: "Inter"
        readonly property color bg: theme.background
        readonly property color label: theme.foreground
        readonly property color labelSecondary: theme.muted
        readonly property color labelTertiary: a(label, 0.40)
        readonly property color tint: theme.accent
        readonly property color tintText: theme.accentForeground
        readonly property color separator: a(label, 0.10)
        readonly property color fill: a(label, 0.06)
        readonly property color fillPressed: a(label, 0.12)
        readonly property color bgElevated: Qt.tint(bg, a(label, 0.06))
        readonly property color selection: theme.selection
        readonly property color danger: theme.error
        readonly property bool dark: Qt.color(theme.background).hslLightness < 0.5
        // size, weight, tracking in px
        readonly property var type: ({
            title1: [24, Font.Bold, -0.46], title2: [20, Font.DemiBold, -0.3],
            title3: [17, Font.DemiBold, -0.17], body: [14, Font.Normal, 0],
            callout: [13, Font.Normal, 0], footnote: [12, Font.Normal, 0.06],
            caption: [11, Font.Normal, 0.11] })
    }

    component Txt: Text {
        property string role: "body"
        font.family: pal.family
        font.pixelSize: pal.type[role][0]
        font.weight: pal.type[role][1]
        font.letterSpacing: pal.type[role][2]
        color: pal.label
        textFormat: Text.PlainText
        elide: Text.ElideRight
    }

    // Lucide path data, drawn as an SVG so it stays crisp and takes the theme color.
    component Icon: Image {
        property var paths: []
        property color tone: pal.label
        property int size: 16
        width: size
        height: size
        sourceSize: Qt.size(size * 2, size * 2)
        source: "data:image/svg+xml," + encodeURIComponent(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='" + tone
            + "' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
            + paths.map(d => "<path d='" + d + "'/>").join("") + "</svg>")
    }

    component Keycap: Rectangle {
        property string text
        height: 20
        width: Math.max(20, cap.implicitWidth + 12)
        radius: 6
        color: pal.fillPressed
        Txt {
            id: cap
            anchors.centerIn: parent
            role: "caption"
            text: parent.text
            font.weight: Font.DemiBold
        }
    }

    // A chord is keys pressed together; alternatives sit further apart.
    component Chords: Row {
        property var chords: []
        spacing: 8
        Repeater {
            model: chords
            delegate: Row {
                required property var modelData
                spacing: 2
                Repeater {
                    model: modelData
                    delegate: Keycap { required property string modelData; text: modelData }
                }
            }
        }
    }

    component Hint: Row {
        property var keys: []
        property string word
        spacing: 6
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Repeater {
                model: keys
                delegate: Keycap { required property string modelData; text: modelData }
            }
        }
        Txt {
            anchors.verticalCenter: parent.verticalCenter
            role: "footnote"
            text: word
            color: pal.labelSecondary
        }
    }

    // The add and search fields: a filled field with a leading glyph in the
    // row numbers' 24px slot, so the text meets the titles' edge.
    component Field: Basic.TextField {
        id: field
        property var glyph: []
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        placeholderTextColor: pal.labelTertiary
        color: pal.label
        palette.text: pal.label
        selectionColor: pal.selection
        selectedTextColor: pal.label
        font.family: pal.family
        font.pixelSize: 14
        leftPadding: 48
        rightPadding: 12
        verticalAlignment: TextInput.AlignVCenter
        background: Rectangle { radius: field.height / 2; color: pal.fill }
        Icon {
            x: 12 + 24 - width
            anchors.verticalCenter: parent.verticalCenter
            paths: field.glyph
            tone: pal.labelSecondary
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: pal.tint
            visible: field.activeFocus
        }
    }

    component Segment: Item {
        property bool done
        property string label
        property int count
        readonly property bool active: win.showDone === done
        implicitWidth: segText.implicitWidth + 24
        implicitHeight: 24
        Row {
            id: segText
            anchors.centerIn: parent
            spacing: 4
            Txt {
                role: "footnote"
                text: label
                color: active ? pal.label : pal.labelSecondary
                font.weight: active ? Font.DemiBold : Font.Normal
            }
            Txt {
                role: "footnote"
                text: count.toLocaleString(Qt.locale(), 'f', 0)
                // Secondary grey is too faint on the thumb.
                color: active ? pal.a(pal.label, 0.75) : pal.labelSecondary
                font.features: ({ "tnum": 1 })
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: if (!active) win.switchView()
        }
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top; margins: 20; bottomMargin: 0 }
            spacing: 12

            // Header: the two lists, switched with Tab.
            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                spacing: 16

                Txt {
                    role: "title1"
                    text: qsTr("Reading List")
                    Layout.fillWidth: true
                }
                Rectangle {
                    id: track
                    implicitWidth: segments.implicitWidth + 4
                    implicitHeight: 28
                    radius: height / 2
                    color: pal.fillPressed

                    Rectangle {
                        readonly property Item seg: win.showDone ? readSeg : queueSeg
                        x: 2 + seg.x
                        y: 2
                        width: seg.width
                        height: 24
                        radius: 12
                        color: pal.dark ? Qt.tint(Qt.tint(pal.bg, pal.fillPressed), pal.a(pal.tint, 0.35))
                                        : Qt.rgba(1, 1, 1, 0.9)
                        border.width: pal.dark ? 0 : 1
                        border.color: pal.separator
                        Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    }
                    Row {
                        id: segments
                        x: 2
                        y: 2
                        Segment { id: queueSeg; done: false; label: qsTr("Queue"); count: backend.queue.length }
                        Segment { id: readSeg; done: true; label: qsTr("Read"); count: backend.done.length }
                    }
                }
            }

            Field {
                id: addField
                visible: win.adding
                placeholderText: qsTr("arXiv link or id, or any web address")
                glyph: ["M5 12h14", "M12 5v14"]
                onAccepted: {
                    if (text.trim() !== "") {
                        win.selectTopNext = true
                        backend.add(text)
                    }
                    win.stopAdding()
                }
                Keys.onEscapePressed: win.stopAdding()
            }

            // Filters the open list as you type; arrows and Enter act on the
            // matches, Esc clears the filter.
            Field {
                id: searchField
                visible: win.searching
                placeholderText: qsTr("Search titles and authors")
                glyph: ["m21 21-4.34-4.34", "M19 11a8 8 0 1 1-16 0 8 8 0 0 1 16 0"]
                onTextChanged: {
                    win.query = text
                    // Typing picks the top match; clearing on Esc keeps the found paper.
                    Qt.callLater(win.searching ? () => win.select(0) : win.restoreSelection)
                }
                onAccepted: win.openSelected(false)
                Keys.onEscapePressed: win.stopSearch()
                Keys.onPressed: (event) => {
                    const ctrl = event.modifiers & Qt.ControlModifier
                    if (event.key === Qt.Key_Up) win.select(list.currentIndex - 1)
                    else if (event.key === Qt.Key_Down) win.select(list.currentIndex + 1)
                    else if (ctrl && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) win.openSelected(true)
                    else return
                    event.accepted = true
                }
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
                        height: 56

                        HoverHandler { id: hover }
                        Rectangle {
                            anchors.fill: parent
                            anchors.topMargin: 2
                            anchors.bottomMargin: 2
                            radius: 10
                            color: row.selected ? pal.selection : hover.hovered ? pal.fill : "transparent"
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12
                            Txt {
                                visible: !win.showDone
                                role: "footnote"
                                text: (row.modelData.pos + 1).toLocaleString(Qt.locale(), 'f', 0)
                                color: row.selected ? pal.label : pal.labelSecondary
                                font.features: ({ "tnum": 1 })
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 24
                            }
                            Item {
                                visible: win.showDone
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 16
                                Icon {
                                    anchors.right: parent.right
                                    paths: ["M20 6 9 17l-5-5"]
                                    tone: pal.tint
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Txt {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    font.pixelSize: 15
                                    font.weight: row.selected ? Font.DemiBold : Font.Normal
                                }
                                Txt {
                                    Layout.fillWidth: true
                                    role: "footnote"
                                    text: row.modelData.pending ? qsTr("Fetching the title…")
                                          : win.showDone && row.modelData.read !== ""
                                            ? row.modelData.subtitle + "  ·  " + qsTr("read %1").arg(row.modelData.read)
                                            : row.modelData.subtitle
                                    // Muted text is blended for the window
                                    // background and fades on the selection.
                                    color: row.selected ? pal.a(pal.label, 0.75) : pal.labelSecondary
                                }
                            }
                        }
                        // Text column only: from the title's edge to the content edge.
                        Rectangle {
                            visible: !row.selected && row.index + 1 !== list.currentIndex && row.index < list.count - 1
                            x: 48
                            width: parent.width - 48 - 12
                            y: parent.height - 1
                            height: 1
                            color: pal.separator
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

                Column {
                    anchors.centerIn: parent
                    visible: win.papers.length === 0 && !win.abstractOpen
                    spacing: 4
                    Txt {
                        anchors.horizontalCenter: parent.horizontalCenter
                        role: "title3"
                        text: win.query !== "" ? qsTr("No matches")
                              : win.showDone ? qsTr("Nothing read yet") : qsTr("Nothing queued")
                    }
                    Txt {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !win.showDone && win.query === ""
                        role: "callout"
                        text: qsTr("Press A to add a paper.")
                        color: pal.labelSecondary
                    }
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
                        y: 4
                        width: Math.min(640, abstractView.width - 16)
                        spacing: 12
                        Txt {
                            Layout.fillWidth: true
                            role: "title2"
                            text: win.info.title || ""
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                        }
                        Txt {
                            Layout.fillWidth: true
                            visible: text !== ""
                            role: "callout"
                            text: win.info.authors || ""
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                            color: pal.labelSecondary
                        }
                        Txt {
                            Layout.fillWidth: true
                            role: "footnote"
                            text: [qsTr("%1 of %2").arg(list.currentIndex + 1).arg(win.papers.length),
                                   win.info.published || "",
                                   win.info.arxivId ? "arXiv " + win.info.arxivId : (win.info.url || "")]
                                  .filter(s => s !== "").join("  ·  ")
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                            color: pal.labelSecondary
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: pal.separator }
                        Txt {
                            Layout.fillWidth: true
                            text: win.info.loading ? qsTr("Fetching the abstract…")
                                  : win.info.error ? qsTr("Could not fetch the abstract: %1").arg(win.info.error)
                                  : (win.info.abstract || qsTr("No abstract available."))
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                            // Qt scales the font's own leading, so 1.2 reads as CSS 1.45.
                            lineHeight: 1.2
                            color: win.info.error ? pal.danger : pal.label
                        }
                    }
                }

                // Floats over the list so a status never pushes the layout.
                Rectangle {
                    visible: backend.status !== ""
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    width: Math.min(statusText.implicitWidth + 32, win.width - 40)
                    height: 32
                    radius: 16
                    color: pal.bgElevated
                    border.width: 1
                    border.color: pal.separator
                    Txt {
                        id: statusText
                        anchors.centerIn: parent
                        width: parent.width - 32
                        horizontalAlignment: Text.AlignHCenter
                        role: "footnote"
                        text: backend.status
                        // Statuses end in what to do next ("Ctrl+Z brings it back");
                        // a long title gives way in the middle instead.
                        elide: Text.ElideMiddle
                        color: backend.statusIsError ? pal.danger : pal.label
                    }
                }
            }
        }

        // Key hints. "?" stays; the rest give way from the end in a narrow window.
        Item {
            id: footer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 20; rightMargin: 20; bottomMargin: 8 }
            height: 33

            readonly property var hints: win.searching
                ? [[["Enter"], qsTr("open")], [["↑", "↓"], qsTr("select")], [["Esc"], qsTr("clear")]]
                : win.abstractOpen
                ? [[["Enter"], qsTr("open")], [["↑", "↓"], qsTr("next")],
                   [["PgUp", "PgDn"], qsTr("scroll")], [["Space", "Esc"], qsTr("back")]]
                : win.showDone
                  ? [[["Enter"], qsTr("open")], [["Space"], qsTr("abstract")],
                     [["U"], qsTr("unread")], [["S"], qsTr("search")], [["Tab"], qsTr("queue")]]
                  : [[["Enter"], qsTr("open")], [["Space"], qsTr("abstract")], [["R"], qsTr("read")],
                     [["A"], qsTr("add")], [["S"], qsTr("search")], [["Shift", "↑", "↓"], qsTr("move")]]

            Rectangle { width: parent.width; height: 1; color: pal.separator }

            Item {
                anchors { left: parent.left; right: keysHint.left; rightMargin: 16; top: parent.top; topMargin: 1; bottom: parent.bottom }
                clip: true
                Row {
                    id: hintRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 16
                    Repeater {
                        model: footer.hints
                        delegate: Hint {
                            required property var modelData
                            keys: modelData[0]
                            word: modelData[1]
                            visible: x + width <= hintRow.parent.width
                        }
                    }
                }
            }
            Hint {
                id: keysHint
                anchors.right: parent.right
                y: hintRow.parent.y + hintRow.y
                keys: ["?"]
                word: qsTr("keys")
            }
        }

        // Every shortcut, shown with "?".
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.35)
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 440)
                height: helpGrid.implicitHeight + 2 * helpPad
                radius: 20
                color: pal.bgElevated
                border.color: pal.separator
                border.width: 1

                // Padding gives way before the rows do at the minimum window height.
                readonly property int helpPad: win.height < 480 ? 16 : 24

                Column {
                    id: helpGrid
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: parent.helpPad }
                    Txt {
                        role: "title3"
                        text: qsTr("Keys")
                        bottomPadding: 8
                    }
                    Repeater {
                        model: [
                            { label: qsTr("Open the paper and close"), chords: [["Enter"]] },
                            { label: qsTr("Open the paper, stay open"), chords: [["Ctrl", "Enter"]] },
                            { label: qsTr("Show or hide the abstract"), chords: [["Space"]] },
                            { label: qsTr("Select"), chords: [["↑", "↓"], ["K", "J"]] },
                            { label: qsTr("Move one place"), chords: [["Shift", "↑", "↓"]] },
                            { label: qsTr("Move to the top or bottom"), chords: [["Shift", "↑", "↓"]], note: qsTr("hold") },
                            { label: qsTr("Mark read (in Read: unread)"), chords: [["R"]] },
                            { label: qsTr("Add a paper (link from clipboard)"), chords: [["A"]] },
                            { label: qsTr("Remove"), chords: [["D"], ["Delete"]] },
                            { label: qsTr("Search titles and authors"), chords: [["S"], ["/"]] },
                            { label: qsTr("Switch between Queue and Read"), chords: [["Tab"]] },
                            { label: qsTr("Undo"), chords: [["Ctrl", "Z"]] },
                            { label: qsTr("Redo"), chords: [["Ctrl", "Shift", "Z"]] },
                            { label: qsTr("Close"), chords: [["Esc"], ["Q"]] }
                        ]
                        delegate: Item {
                            required property var modelData
                            width: helpGrid.width
                            // Short windows squeeze the rows so the sheet still fits.
                            height: Math.max(22, Math.min(28, (win.height - 32 - 2 * 16 - 29) / 14))
                            Txt {
                                anchors.left: parent.left
                                anchors.right: chordRow.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                role: "callout"
                                text: modelData.label
                            }
                            Row {
                                id: chordRow
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                Txt {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: !!modelData.note
                                    role: "footnote"
                                    text: modelData.note || ""
                                    color: pal.labelSecondary
                                }
                                Chords { chords: modelData.chords }
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
        case Qt.Key_S:
        case Qt.Key_Slash:
            startSearch()
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
