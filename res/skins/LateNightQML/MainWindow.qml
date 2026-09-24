pragma ComponentBehavior: Bound

import "../../qml" as Skin
import "LateNightTheme"
import "Deck" as LateNightDeck
import "Effects" as LateNightEffects
import "MicAux" as LateNightMicAux
import "Mixer" as LateNightMixer
import "Samplers" as LateNightSamplers
import "Toolbar" as LateNightToolbar
import "Waveforms" as LateNightWaveforms
import Mixxx 1.0 as Mixxx
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root

    required property ApplicationWindow applicationWindow
    property alias menuBar: nativeApplicationMenuLoader.item

    readonly property int activeDeckState: layoutState.effectiveDeckSize
    readonly property int activeDeckHeight: activeDeckState === 0 ? LateNightTheme.miniDeckHeight : (activeDeckState === 1 ? LateNightTheme.compactDeckHeight : LateNightTheme.fullDeckHeight)
    property alias editDeck: toolbar.editDeck
    property var focusedDeck: null
    property alias maximizeLibrary: toolbar.maximizeLibrary
    readonly property int normalDeckState: layoutState.normalizedSavedDeckSize
    readonly property int numDecks: 4
    readonly property int numSamplers: 64
    readonly property bool show4decks: toolbar.show4decks
    property alias showEffects: toolbar.showEffects
    readonly property bool showCompactVuMeters: layoutState.showCompactVuMeters
    readonly property bool showDeckArea: layoutState.showDeckArea
    property alias showMicAux: toolbar.showMicAux
    readonly property bool showMaximizedDecks: toolbar.showMaximizedDecks
    readonly property bool showMixer: toolbar.showMixer
    property alias showSamplers: toolbar.showSamplers
    readonly property bool showWaveforms: toolbar.showWaveforms

    // Waveforms in a separate pane
    property bool waveformsSeparate: false
    property bool waveformsRight: false
    property int waveformPaneWidth: 600
    property int waveformPaneHeight: 120

    // Library in a separate pane
    property bool librarySeparate: false
    property bool libraryRight: true
    property int libraryPaneWidth: 400

    SkinControlBootstrap {
        id: skinControlBootstrap
    }

    Mixxx.ControlProxy {
        id: showCompactVuMetersProxy

        group: "[Skin]"
        key: "show_vumeters_compact"
    }

    Mixxx.ControlProxy {
        id: normalLayoutWaveformsSeparateControl

        group: "[Skin]"
        key: "normal_layout_waveforms_separate"

        onValueChanged: root.waveformsSeparate = value > 0
    }

    Mixxx.ControlProxy {
        id: normalLayoutWaveformsRightControl

        group: "[Skin]"
        key: "normal_layout_waveforms_right"

        onValueChanged: root.waveformsRight = value > 0
    }

    Mixxx.ControlProxy {
        id: normalLayoutLibrarySeparateControl

        group: "[Skin]"
        key: "normal_layout_library_separate"

        onValueChanged: root.librarySeparate = value > 0
    }

    Mixxx.ControlProxy {
        id: normalLayoutLibraryRightControl

        group: "[Skin]"
        key: "normal_layout_library_right"

        onValueChanged: root.libraryRight = value > 0
    }

    LayoutState {
        id: layoutState

        maximizeLibrary: root.maximizeLibrary
        mixerVisible: root.showMixer
        savedDeckSize: toolbar.deckSizeWithoutMixer
        show4decks: root.show4decks
        showCompactVuMetersSetting: showCompactVuMetersProxy.value > 0
        showMaximizedDecks: root.showMaximizedDecks
    }

    function focusLegacyLibrarySearch() {
        Qt.callLater(function() {
            if (library.item) {
                library.item.focusSearch();
            }
        });
    }

    Loader {
        id: nativeApplicationMenuLoader

        active: Qt.platform.os === "osx"

        sourceComponent: Skin.MainMenuBar {
            actions: applicationMenuActions
        }
    }
    Skin.ApplicationMenuCommands {
        id: applicationMenuCommands

        applicationWindow: root.applicationWindow

        onShowDeveloperToolsRequested: {
            developerToolsWindow.show();
            developerToolsWindow.raise();
            developerToolsWindow.requestActivate();
        }
    }
    Skin.ApplicationMenuActions {
        id: applicationMenuActions

        applicationWindow: root.applicationWindow
        commands: applicationMenuCommands
        numberOfDecks: root.show4decks ? root.numDecks : 2

        onFocusLibrarySearchRequested: root.focusLegacyLibrarySearch()
    }
    Skin.DeveloperToolsWindow {
        id: developerToolsWindow

        height: 480
        width: 640
    }
    Skin.LibraryScanSummaryDialog {
    }
    Mixxx.ControlProxy {
        group: "[App]"
        key: "num_decks"

        onInitializedChanged: {
            value = root.numDecks;
        }
    }
    Mixxx.ControlProxy {
        group: "[App]"
        key: "num_samplers"

        onInitializedChanged: {
            value = root.numSamplers;
        }
    }

    ///////////////////////////////////////////////////////////////
    // toolbar + layout area
    // toolbar is always above controls & library
    // toolbar is never above the waveforms if they are separated
    ///////////////////////////////////////////////////////////////
    Item {
        id: content

        anchors.fill: parent

        LateNightToolbar.Toolbar {
            id: toolbar

            applicationMenuActions: applicationMenuActions
            show4decksAvailable: root.height > 515

            // When waveforms -> own pane
            // toolbar not above waveforms

            x: root.waveformsSeparate ? layoutArea.innerAreaX : 0
            y: 0
            width: root.waveformsSeparate ? layoutArea.innerAreaWidth : parent.width

            onFocusLibrarySearchRequested: root.focusLegacyLibrarySearch()
        }

        //////////////////////////////////////////////////////////////////////////////
        // Layout area
        //
        // Waveforms can have their own pane (column) -> always on the outside (L/R)
        // inner content/area = Controls + Library (in any form)
        //
        // Waveforms can be outer left (default) or outer right
        // So it can be
        // - W -spliter- (C & L) or (C & L) -splitter- W
        //
        // - Controls & Library can be split too,
        //   Library can be left or right (default) of controls
        // So it can be
        // - W -spliter- (C -splitter- L) or (C -splitter- L) -splitter- W
        // - W -spliter- (L -splitter- C) or (L -splitter- C) -splitter- W
        //
        // - If waveforms is in separate pane an extra spacer is added to be able
        //   to resize the waveforms, eg for a 720 wide screen above mixer
        //   Waveforms are Top V-Aligned
        // - When Controls & Library are SplitView
        //   - Library takes full colymn height
        //   - Controls are Top V-Aligned
        ////////////////////////////////////////////////////////////////////////////

        Item {
            id: layoutArea

            x: 0
            y: 0
            width: parent.width
            height: parent.height

            // vertical offset for inner content below the toolbar.
            readonly property real contentTop: toolbar.height

            // width waveform column (if separated & shown).
            readonly property real waveformOccupiedWidth: (root.waveformsSeparate && waveforms.shown)
                    ? (waveformColumnHandle.width + root.waveformPaneWidth)
                    : 0

            // left edge of the inner area (controls + library).
            readonly property real innerAreaX: (root.waveformsSeparate && waveforms.shown && !root.waveformsRight)
                    ? (waveformColumnHandle.width + root.waveformPaneWidth)
                    : 0

            // width of the inner area.
            readonly property real innerAreaWidth: Math.max(0, width - waveformOccupiedWidth)

            //////////////////////////////////////////////////////////////////
            // waveforms column
            // - stacked mode -> below toolbar, height = waveformPaneHeight
            // - separate mode -> own column, full height beside toolbar,
            //   left or right
            //////////////////////////////////////////////////////////////////
            LateNightWaveforms.WaveformStack {
                id: waveforms

                readonly property bool horizontalLayout: root.waveformsSeparate
                readonly property bool shown: root.showWaveforms && !root.maximizeLibrary

                show4decks: root.show4decks
                visible: shown

                x: horizontalLayout
                        ? (shown
                           ? (root.waveformsRight
                              ? waveformColumnHandle.x + waveformColumnHandle.width
                              : 0)
                           : 0)
                        : 0
                y: horizontalLayout ? 0 : layoutArea.contentTop
                width: horizontalLayout
                        ? (shown
                           ? (root.waveformsRight
                              ? Math.max(0, layoutArea.width - waveformColumnHandle.x - waveformColumnHandle.width)
                              : Math.max(0, waveformColumnHandle.x))
                           : 0)
                        : layoutArea.width
                height: horizontalLayout
                        ? (shown ? root.waveformPaneHeight : 0)
                        : (shown
                           ? Math.max(0, waveformRowHandle.y - layoutArea.contentTop)
                           : 0)

                Skin.FadeBehavior on visible {
                    fadeTarget: waveforms
                }
            }

            //////////////////////////////////////////////////////////////////
            // Horizontal splitter (=handle) INSIDE the waveform column
            // (separate mode only).
            // at waveforms.y + waveformPaneHeight at the bottom
            // of the waveform column
            //////////////////////////////////////////////////////////////////
            MouseArea {
                id: waveformColumnHeightHandle

                property real dragStartPos: 0
                property real dragStartSize: 0

                cursorShape: Qt.SplitVCursor
                visible: root.waveformsSeparate && waveforms.shown
                x: waveforms.x
                y: waveforms.y + root.waveformPaneHeight
                width: waveforms.width
                height: 8

                onPressed: function(mouse) {
                    dragStartPos = mouse.y + waveformColumnHeightHandle.y;
                    dragStartSize = root.waveformPaneHeight;
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return;
                    }
                    const dy = (mouse.y + waveformColumnHeightHandle.y) - dragStartPos;
                    const maxH = layoutArea.height - 80;
                    root.waveformPaneHeight = Math.max(80, Math.min(maxH, dragStartSize + dy));
                }

                Rectangle {
                    anchors.fill: parent
                    color: waveformColumnHeightHandle.pressed
                            ? LateNightTheme.libraryPanelSplitterHandleActive
                            : LateNightTheme.libraryPanelSplitterBackground
                }
            }

            //////////////////////////////////////////////////////////////////
            // Blank spacer below the horizontal handle in the waveform column
            //////////////////////////////////////////////////////////////////
            Item {
                id: waveformColumnSpacer

                visible: root.waveformsSeparate && waveforms.shown
                x: waveforms.x
                y: waveformColumnHeightHandle.y + waveformColumnHeightHandle.height
                width: waveforms.width
                height: Math.max(0, layoutArea.height - y)
            }

            ////////////////////////////////////////////////////////////////////
            // vertical splitter (=handle) BETWEEN waveforms and the inner area
            // (separate mode only) spanning the full window height
            // no toolbar
            ////////////////////////////////////////////////////////////////////
            MouseArea {
                id: waveformColumnHandle

                property real dragStartPos: 0
                property real dragStartSize: 0

                cursorShape: Qt.SplitHCursor
                visible: root.waveformsSeparate && waveforms.shown
                x: root.waveformsRight
                        ? (layoutArea.width - root.waveformPaneWidth - width)
                        : root.waveformPaneWidth
                y: 0
                width: 8
                height: layoutArea.height

                onPressed: function(mouse) {
                    dragStartPos = mouse.x + waveformColumnHandle.x;
                    dragStartSize = root.waveformPaneWidth;
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return;
                    }
                    const dx = (mouse.x + waveformColumnHandle.x) - dragStartPos;
                    const effectiveDx = root.waveformsRight ? -dx : dx;
                    const maxW = layoutArea.width - 200;
                    root.waveformPaneWidth = Math.max(120, Math.min(maxW, dragStartSize + effectiveDx));
                }

                Rectangle {
                    anchors.fill: parent
                    color: waveformColumnHandle.pressed
                            ? LateNightTheme.libraryPanelSplitterHandleActive
                            : LateNightTheme.libraryPanelSplitterBackground
                }
            }

            //////////////////////////////////////////////////////////////////
            // Vertical handle in STACKED mode (1-pane = default layout)
            // between waveforms and the deck pane. Sits below the toolbar.
            //////////////////////////////////////////////////////////////////
            MouseArea {
                id: waveformRowHandle

                property real dragStartPos: 0
                property real dragStartSize: 0

                cursorShape: Qt.SplitVCursor
                visible: !root.waveformsSeparate && waveforms.shown
                x: 0
                y: layoutArea.contentTop + root.waveformPaneHeight
                width: layoutArea.width
                height: 8

                onPressed: function(mouse) {
                    dragStartPos = mouse.y + waveformRowHandle.y;
                    dragStartSize = root.waveformPaneHeight;
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return;
                    }
                    const dy = (mouse.y + waveformRowHandle.y) - dragStartPos;
                    const maxH = layoutArea.height - 200;
                    root.waveformPaneHeight = Math.max(60, Math.min(maxH, dragStartSize + dy));
                }

                Rectangle {
                    anchors.fill: parent
                    color: waveformRowHandle.pressed
                            ? LateNightTheme.libraryPanelSplitterHandleActive
                            : LateNightTheme.libraryPanelSplitterBackground
                }
            }

            //////////////////////////////////////////////////////////////////
            // vertical handle for the Library column
            // -> inside the inner area (C & L), using libraryRight directly
            // -> under the toolbar
            //////////////////////////////////////////////////////////////////
            MouseArea {
                id: libraryColumnHandle

                property real dragStartPos: 0
                property real dragStartSize: 0

                cursorShape: Qt.SplitHCursor
                visible: root.librarySeparate
                x: root.libraryRight
                        ? (layoutArea.innerAreaX + layoutArea.innerAreaWidth - root.libraryPaneWidth - width)
                        : (layoutArea.innerAreaX + root.libraryPaneWidth)
                y: layoutArea.contentTop
                width: 8
                height: layoutArea.height - layoutArea.contentTop

                onPressed: function(mouse) {
                    dragStartPos = mouse.x + libraryColumnHandle.x;
                    dragStartSize = root.libraryPaneWidth;
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return;
                    }
                    const dx = (mouse.x + libraryColumnHandle.x) - dragStartPos;
                    const effectiveDx = root.libraryRight ? -dx : dx;
                    const maxW = layoutArea.innerAreaWidth - 200;
                    root.libraryPaneWidth = Math.max(120, Math.min(maxW, dragStartSize + effectiveDx));
                }

                Rectangle {
                    anchors.fill: parent
                    color: libraryColumnHandle.pressed
                            ? LateNightTheme.libraryPanelSplitterHandleActive
                            : LateNightTheme.libraryPanelSplitterBackground
                }
            }

            //////////////////////////////////////////////////////////////////
            // deckpane (decks + mixer + effects + samplers + mic/aux)
            // Top V-aligned, spacer fills the space left over in the inner
            // area under the Library
            // If Library is separated column fills the space
            // Under toolbar
            //////////////////////////////////////////////////////////////////
            Rectangle {
                id: deckPane

                color: LateNightTheme.layoutGutterColor
                readonly property int deckRowCount: root.show4decks ? 2 : 1
                readonly property real basePaneHeight: Math.max(deckRowsHeight, mixerLayoutVisible ? mixer.implicitHeight + LateNightTheme.deckRowGutter : 0)
                readonly property real deckRowHeight: visibleDeckHeight > 0
                        ? (deckStackHeight - LateNightTheme.deckRowGutter * (deckRowCount - 1)) / deckRowCount
                        : 0
                readonly property real deckRowsHeight: visibleDeckHeight > 0
                        ? visibleDeckHeight * (root.show4decks ? 2 : 1) + LateNightTheme.deckRowGutter * (root.show4decks ? 2 : 1)
                        : 0
                readonly property int deckSideMargin: root.showMixer && !root.maximizeLibrary ? LateNightTheme.deckMixerGutter : 2
                readonly property real deckStackHeight: basePaneHeight - LateNightTheme.deckRowGutter
                readonly property bool mixerLayoutVisible: root.showMixer && !root.maximizeLibrary
                readonly property real requiredPaneHeight: basePaneHeight + effectsSection.height + samplersSection.height + micAuxSection.height
                readonly property real visibleDeckHeight: root.maximizeLibrary ? (root.showMaximizedDecks ? LateNightTheme.miniDeckHeight : 0) : root.activeDeckHeight

                // offset from top of deckpane to Library area
                // when librarySeparate = false.
                readonly property real libraryTopOffset: micAuxSection.y + micAuxSection.height

                x: {
                    if (root.librarySeparate && !root.libraryRight) {
                        return libraryColumnHandle.x + libraryColumnHandle.width;
                    }
                    return layoutArea.innerAreaX;
                }
                y: (!root.waveformsSeparate && waveforms.shown)
                        ? (waveformRowHandle.y + waveformRowHandle.height)
                        : layoutArea.contentTop
                width: {
                    let w = layoutArea.innerAreaWidth;
                    if (root.librarySeparate) {
                        w -= libraryColumnHandle.width + root.libraryPaneWidth;
                    }
                    return Math.max(0, w);
                }
                height: (!root.waveformsSeparate && waveforms.shown)
                        ? Math.max(0, layoutArea.height - y)
                        : (layoutArea.height - layoutArea.contentTop)

                Item {
                    id: deckFirstRowBottom

                    height: 0
                    y: deckPane.deckRowHeight
                }
                Item {
                    id: deckStackBottom

                    height: 0
                    y: deckPane.deckStackHeight
                }
                LateNightDeck.Deck {
                    id: deck1

                    deckState: root.maximizeLibrary ? LateNightDeck.Deck.Mini : root.activeDeckState
                    editMode: root.editDeck
                    group: "[Channel1]"
                    visible: !root.maximizeLibrary || root.showMaximizedDecks
                    onToggleFocus: {
                        root.focusedDeck = (root.focusedDeck === deck1) ? null : deck1;
                    }

                    anchors {
                        bottom: root.show4decks ? deckFirstRowBottom.top : deckStackBottom.top
                        left: parent.left
                        right: mixer.left
                        rightMargin: deckPane.deckSideMargin
                        top: parent.top
                    }

                    states: [
                        State {
                            when: root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.right: compactVuSlot.left
                                target: deck1
                            }
                        },
                        State {
                            when: !deckPane.mixerLayoutVisible && !root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.right: parent.horizontalCenter
                                target: deck1
                            }
                        },
                        State {
                            when: root.maximizeLibrary

                            AnchorChanges {
                                anchors.right: parent.horizontalCenter
                                target: deck1
                            }
                        }
                    ]
                }
                Item {
                    id: compactVuSlot

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    height: root.showCompactVuMeters ? deckPane.deckStackHeight : 0
                    visible: root.showCompactVuMeters
                    width: root.showCompactVuMeters ? LateNightTheme.compactVuSlotWidth : 0
                    z: 10

                    Rectangle {
                        anchors.fill: parent
                        color: LateNightTheme.compactVuGutterColor
                    }
                    LateNightMixer.CompactCenterVuMeters {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        show4decks: root.show4decks
                    }
                }
                LateNightMixer.Mixer {
                    id: mixer

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    groups: [deck1.group, deck2.group, deck3.group, deck4.group]
                    height: deckPane.mixerLayoutVisible ? deckPane.deckStackHeight : 0
                    show4decks: root.show4decks
                    visible: root.showMixer && !root.maximizeLibrary
                    width: deckPane.mixerLayoutVisible ? implicitWidth : 0

                    states: [
                        State {
                            when: root.showMixer && root.focusedDeck === deck1 && root.width < 1400 && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.horizontalCenter: parent.right
                                target: mixer
                            }
                            PropertyChanges {
                                target: deck1
                                width: root.width - (mixer.width / 2)
                            }
                        },
                        State {
                            when: root.showMixer && root.focusedDeck === deck2 && root.width < 1400 && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.horizontalCenter: parent.left
                                target: mixer
                            }
                            PropertyChanges {
                                target: deck2
                                width: root.width - (mixer.width / 2)
                            }
                        },
                        State {
                            when: root.showMixer && (!root.focusedDeck || root.width > 1400) && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.horizontalCenter: parent.horizontalCenter
                                target: mixer
                            }
                            PropertyChanges {
                                target: deck1
                                width: (root.width - mixer.width) / 2
                            }
                            PropertyChanges {
                                target: deck2
                                width: (root.width - mixer.width) / 2
                            }
                        },
                        State {
                            when: root.maximizeLibrary

                            AnchorChanges {
                                anchors.horizontalCenter: parent.horizontalCenter
                                target: mixer
                            }
                            PropertyChanges {
                                target: deck1
                                width: root.width / 2
                            }
                            PropertyChanges {
                                target: deck2
                                width: root.width / 2
                            }
                        }
                    ]
                    transitions: Transition {
                        AnchorAnimation {
                            duration: 200
                        }
                    }
                    Skin.FadeBehavior on visible {
                        fadeTarget: mixer
                    }
                    Behavior on width {
                        SpringAnimation {
                            id: mixerWidthAnimation

                            damping: 0.2
                            duration: 500
                            spring: 2
                        }
                    }
                }
                LateNightDeck.Deck {
                    id: deck2

                    deckState: root.maximizeLibrary ? LateNightDeck.Deck.Mini : root.activeDeckState
                    editMode: root.editDeck
                    group: "[Channel2]"
                    visible: !root.maximizeLibrary || root.showMaximizedDecks
                    onToggleFocus: {
                        root.focusedDeck = (root.focusedDeck === deck2) ? null : deck2;
                    }

                    anchors {
                        bottom: root.show4decks ? deckFirstRowBottom.top : deckStackBottom.top
                        left: mixer.right
                        leftMargin: deckPane.deckSideMargin
                        right: parent.right
                        top: parent.top
                    }

                    states: [
                        State {
                            when: root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.left: compactVuSlot.right
                                target: deck2
                            }
                        },
                        State {
                            when: !deckPane.mixerLayoutVisible && !root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.left: parent.horizontalCenter
                                target: deck2
                            }
                        },
                        State {
                            when: root.maximizeLibrary

                            AnchorChanges {
                                anchors.left: parent.horizontalCenter
                                target: deck2
                            }
                        }
                    ]
                }
                Loader {
                    id: deck3

                    readonly property string group: "[Channel3]"

                    active: root.show4decks && (!root.maximizeLibrary || root.showMaximizedDecks)
                    clip: true
                    sourceComponent: Component {
                        LateNightDeck.Deck {
                            anchors.fill: parent
                            deckState: root.maximizeLibrary ? LateNightDeck.Deck.Mini : root.activeDeckState
                            editMode: root.editDeck
                            group: deck3.group
                        }
                    }
                    states: [
                        State {
                            when: root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.right: compactVuSlot.left
                                target: deck3
                            }
                        },
                        State {
                            when: !deckPane.mixerLayoutVisible && !root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.right: parent.horizontalCenter
                                target: deck3
                            }
                        },
                        State {
                            when: root.maximizeLibrary

                            AnchorChanges {
                                anchors.right: parent.horizontalCenter
                                target: deck3
                            }
                        }
                    ]

                    anchors {
                        bottom: deckStackBottom.top
                        left: parent.left
                        right: mixer.left
                        rightMargin: deckPane.deckSideMargin
                        top: deckFirstRowBottom.bottom
                        topMargin: LateNightTheme.deckRowGutter
                    }
                }
                Loader {
                    id: deck4

                    readonly property string group: "[Channel4]"

                    active: root.show4decks && (!root.maximizeLibrary || root.showMaximizedDecks)
                    clip: true
                    sourceComponent: Component {
                        LateNightDeck.Deck {
                            anchors.fill: parent
                            deckState: root.maximizeLibrary ? LateNightDeck.Deck.Mini : root.activeDeckState
                            editMode: root.editDeck
                            group: deck4.group
                        }
                    }
                    states: [
                        State {
                            when: root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.left: compactVuSlot.right
                                target: deck4
                            }
                        },
                        State {
                            when: !deckPane.mixerLayoutVisible && !root.showCompactVuMeters && !root.maximizeLibrary

                            AnchorChanges {
                                anchors.left: parent.horizontalCenter
                                target: deck4
                            }
                        },
                        State {
                            when: root.maximizeLibrary

                            AnchorChanges {
                                anchors.left: parent.horizontalCenter
                                target: deck4
                            }
                        }
                    ]

                    anchors {
                        bottom: deckStackBottom.top
                        left: mixer.right
                        leftMargin: deckPane.deckSideMargin
                        right: parent.right
                        top: deckFirstRowBottom.bottom
                        topMargin: LateNightTheme.deckRowGutter
                    }
                }
                Item {
                    id: effectsSection

                    clip: true
                    height: root.showEffects && !root.maximizeLibrary ? effectsRack.implicitHeight : 0
                    opacity: root.showEffects && !root.maximizeLibrary ? 1 : 0
                    visible: height > 0
                    width: parent.width
                    y: deckPane.basePaneHeight
                    z: 2

                    Behavior on height {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 120
                        }
                    }

                    LateNightEffects.EffectsRack {
                        id: effectsRack

                        anchors.fill: parent
                        leftUnitEnd: deckPane.mixerLayoutVisible || root.showCompactVuMeters
                                ? Math.round((effectsRack.width - effectsRack.unitSpacing) / 2)
                                : deck1.x + deck1.width
                        rightUnitStart: deckPane.mixerLayoutVisible || root.showCompactVuMeters
                                ? Math.round((effectsRack.width + effectsRack.unitSpacing) / 2)
                                : deck2.x
                    }
                }
                Item {
                    id: samplersSection

                    clip: true
                    height: root.showSamplers && !root.maximizeLibrary ? samplers.implicitHeight : 0
                    opacity: root.showSamplers && !root.maximizeLibrary ? 1 : 0
                    visible: height > 0
                    width: parent.width
                    y: effectsSection.y + effectsSection.height
                    z: 2

                    Behavior on height {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 120
                        }
                    }

                    LateNightSamplers.SamplersRack {
                        id: samplers

                        anchors.fill: parent
                    }
                }
                Item {
                    id: micAuxSection

                    clip: true
                    height: root.showMicAux && !root.maximizeLibrary ? micAuxRack.implicitHeight : 0
                    opacity: root.showMicAux && !root.maximizeLibrary ? 1 : 0
                    visible: height > 0
                    width: parent.width
                    y: samplersSection.y + samplersSection.height
                    z: 2

                    Behavior on height {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation {
                            duration: 120
                        }
                    }

                    LateNightMicAux.MicAuxRack {
                        id: micAuxRack

                        anchors.fill: parent
                    }
                }
            }

            ///////////////////////////////////////////////////////////
            // Library
            // - librarySeparate = false
            //   -> Library under Controls
            //
            // - librarySeparate = true
            //   -> full-height column inside the
            //   inner area, at the outside decided by libraryRight,
            //   under the toolbar.
            //////////////////////////////////////////////////////////
            Loader {
                id: library

                active: true

                sourceComponent: Component {
                    Library {
                        anchors.fill: parent
                    }
                }

                x: root.librarySeparate
                        ? (root.libraryRight
                           ? libraryColumnHandle.x + libraryColumnHandle.width
                           : layoutArea.innerAreaX)
                        : deckPane.x
                y: root.librarySeparate
                        ? layoutArea.contentTop
                        : (deckPane.y + deckPane.libraryTopOffset)
                width: root.librarySeparate
                        ? root.libraryPaneWidth
                        : deckPane.width
                height: root.librarySeparate
                        ? (layoutArea.height - layoutArea.contentTop)
                        : Math.max(0, deckPane.height - deckPane.libraryTopOffset)

                states: [
                    State {
                        when: root.maximizeLibrary && !root.showMaximizedDecks

                        AnchorChanges {
                            anchors.top: parent.top
                            target: library
                        }
                    },
                    State {
                        when: root.maximizeLibrary && root.showMaximizedDecks && root.show4decks

                            AnchorChanges {
                                anchors.top: deck4.bottom
                                target: library
                            }
                    },
                    State {
                        when: root.maximizeLibrary && root.showMaximizedDecks && !root.show4decks

                            AnchorChanges {
                                anchors.top: deck1.bottom
                                target: library
                            }
                    }
                ]
            }
        }
    }
}
