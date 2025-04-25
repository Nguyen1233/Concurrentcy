import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MyApp 1.0

ApplicationWindow {
    visible: true
    width: 400
    height: 600
    title: "QtConcurrent Image Loading"

    Worker {
        id: worker
        onTaskFinished: {
            messageDialog.text = "Load Done!"
            messageDialog.open()
            console.log("Task finished, image paths:", imagePaths.length)
        }
        onErrorOccurred: function(error) {
            messageDialog.text = error
            messageDialog.open()
            console.log("Error occurred:", error)
        }
        onImagePathsChanged: {
            console.log("Image paths updated:", imagePaths.length)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        ProgressBar {
            id: progressBar
            Layout.fillWidth: true
            value: worker.progress
        }

        ListView {
            id: imageList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: worker.imagePaths
            delegate: Image {
                width: parent.width
                height: 200
                source: "file://" + modelData + "#t=" + Date.now()
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: true // load img async
                onStatusChanged: {
                    console.log("Image status for", modelData, ":", status)
                    if (status === Image.Ready) {
                        console.log("Image loaded:", modelData)
                    } else if (status === Image.Error) {
                        console.log("Error loading image:", modelData)
                    }
                }
            }
            clip: true
            cacheBuffer: 1000 // Lim buffer decrise load
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10

            Button {
                text: "Start Loading"
                onClicked: worker.startTask()
            }

            Button {
                text: "Cancel"
                onClicked: worker.cancelTask()
            }
        }
    }

    Dialog {
        id: messageDialog
        title: "Pump!!!!!!"
        standardButtons: Dialog.Ok
        implicitWidth: 300
        modal: true // not efect by  UI thread
        property alias text: messageLabel.text
        Label {
            id: messageLabel
            text: ""
        }
        onAccepted: {
            console.log("Dialog closed")
        }
    }
}
