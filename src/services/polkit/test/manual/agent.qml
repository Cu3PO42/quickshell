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
                text: polkitAgent.message
                wrapMode: Text.Wrap
                font.bold: true
            }

            Label {
                visible: !!polkitAgent.subMessage
                text: polkitAgent.subMessage ? polkitAgent.subMessage.text : ""
                wrapMode: Text.Wrap
                opacity: 0.8
            }

            TextInput {
                id: passwordInput
                echoMode: polkitAgent.inputRequest && polkitAgent.inputRequest.echo
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
                    enabled: passwordInput.text.length > 0 || (polkitAgent.inputRequest && polkitAgent.inputRequest.echo)
                    onClicked: {
                        polkitAgent.submit(passwordInput.text)
                        passwordInput.text = ""
                        passwordInput.forceActiveFocus()
                    }
                }
                Button {
                    text: "Cancel"
                    visible: polkitAgent.isActive
                    onClicked: {
                        polkitAgent.cancelAuthenticationRequest()
                        passwordInput.text = ""
                    }
                }
            }
        }
    }

	PolkitAgent {
		id: polkitAgent
		path: "/org/quickshell/PolkitAgent"
		onInputRequestChanged: passwordInput.forceActiveFocus()
	}
}
