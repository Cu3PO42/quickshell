import Quickshell
import Quickshell.Services.Polkit
import QtQuick
import QtQuick.Controls

Scope {
	id: root

    FloatingWindow {
        title: "Authentication Required"

        visible: polkitAgent.isActive

        Column {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            Label {
                text: polkitAgent.flow?.message ?? ""
                wrapMode: Text.Wrap
                font.bold: true
            }

            Label {
                visible: polkitAgent.flow?.supplementaryMessage.length > 0
                text: polkitAgent.flow?.supplementaryMessage ?? ""
                wrapMode: Text.Wrap
                opacity: 0.8
            }

            Label {
                text: polkitAgent.flow?.inputPrompt ?? ""
                wrapMode: Text.Wrap
            }

            TextInput {
                id: passwordInput
                echoMode: polkitAgent.flow?.responseVisible
                          ? TextInput.Normal : TextInput.Password
                selectByMouse: true
                width: parent.width
                onAccepted: okButton.clicked()
            }

            Row {
                spacing: 8
                Button {
                    id: okButton
                    text: "OK"
                    enabled: passwordInput.text.length > 0 || !!polkitAgent.flow?.isResponseRequired
                    onClicked: {
                        polkitAgent.flow.submit(passwordInput.text)
                        passwordInput.text = ""
                        passwordInput.forceActiveFocus()
                    }
                }
                Button {
                    text: "Cancel"
                    visible: polkitAgent.isActive
                    onClicked: {
                        polkitAgent.flow.cancelAuthenticationRequest()
                        passwordInput.text = ""
                    }
                }
            }
        }

		Connections {
			target: polkitAgent.flow
			function onResponseRequestChanged() {
				passwordInput.text = ""
                if (polkitAgent.flow.isResponseRequired)
                    passwordInput.forceActiveFocus()
			}
		}
    }

	PolkitAgent {
		id: polkitAgent
		path: "/org/quickshell/PolkitAgent"
	}
}
