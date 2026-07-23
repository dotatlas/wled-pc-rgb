// MainWindow — device inspector + colour/mode/brightness control + motherboard panel.
#pragma once
#include <QMainWindow>

class QTreeWidget;
class QLabel;
class QSlider;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void refresh();            // connect to OpenRGB and rebuild the tree
    void setSelectedColor();   // colour (scaled by brightness) → selected device
    void setSelectedMode();    // activate the selected mode row on its device
    void setAllColor();        // colour → every device at once

private:
    QColor pickColour();       // shared colour dialog + brightness scaling
    QTreeWidget* tree_   = nullptr;
    QLabel*      status_ = nullptr;
    QLabel*      mobo_   = nullptr;
    QSlider*     bright_ = nullptr;
};
