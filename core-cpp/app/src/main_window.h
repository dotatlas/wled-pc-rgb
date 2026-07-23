// MainWindow — device inspector + colour/brightness control + motherboard panel.
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
    void setSelectedColor();   // apply a picked colour (scaled by brightness) to the selected device

private:
    QTreeWidget* tree_   = nullptr;
    QLabel*      status_ = nullptr;
    QLabel*      mobo_   = nullptr;
    QSlider*     bright_ = nullptr;
};
