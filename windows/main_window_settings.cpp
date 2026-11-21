#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "main_window.h"

#include "lnplayer_windows.h"
#include "player_icons.h"

#include <algorithm>
#include <d3d11.h>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QByteArray>
#include <QFont>
#include <QFontMetrics>
#include <QFontComboBox>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDir>
#include <QDesktopServices>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QCheckBox>
#include <QColor>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QScreen>
#include <QDropEvent>
#include <QEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QListView>
#include <QListWidgetItem>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyleOptionSlider>
#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QShortcut>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QStringDecoder>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStandardPaths>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif


namespace
{
 application_data_directory()
{
    static const QString directory = []
    {
        const QString application_directory = QCoreApplication::applicationDirPath();
        const QString portable_directory = QDir(application_directory).filePath(
            QStringLiteral("Data"));
        if (QDir().mkpath(portable_directory))
        {
            QFile write_probe(QDir(portable_directory).filePath(
                QStringLiteral(".write_test")));
            if (write_probe.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                write_probe.close();
                QFile::remove(write_probe.fileName());
                return portable_directory;
            }
        }

        const QString local_directory = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        const QString fallback_directory = QDir(local_directory).filePath(
            QStringLiteral("Data"));
        QDir().mkpath(fallback_directory);
        return fallback_directory;
    }();
    return directory;
}

 settings_file_path()
{
    return QDir(application_data_directory()).filePath(
        QStringLiteral("settings.ini"));
}
}

void MainWindow::openVideoSettings()
{
    if (mode_ != PlayerMode::Video)
        return;

    // Keep settings as a real top-level window, independent of the custom
    // frameless player window and its native title-bar implementation.
    QDialog dialog(this, Qt::Window);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setObjectName(QStringLiteral("settingsDialog"));
    dialog.setWindowTitle(QStringLiteral("LNPlayer 播放器设置"));
    dialog.setWindowIcon(PlayerIcons::make(PlayerIcons::Name::Settings));
    QScreen* settings_screen = screen();
    if (!settings_screen)
        settings_screen = QApplication::primaryScreen();
    const QRect available_geometry = settings_screen->availableGeometry();
    dialog.setMinimumSize(760, 480);
    dialog.resize(
        std::clamp(available_geometry.width() - 64, 760, 980),
        std::clamp(available_geometry.height() - 64, 480, 700));
    dialog.move(available_geometry.center() - dialog.rect().center());
    dialog.setSizeGripEnabled(true);
    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    auto* content = new QHBoxLayout();
    content->setSpacing(14);
    auto* navigation = new QListWidget(&dialog);
    navigation->setObjectName(QStringLiteral("settingsNavigation"));
    navigation->setSelectionMode(QAbstractItemView::SingleSelection);
    navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    navigation->setFixedWidth(176);
    navigation->setIconSize(QSize(18, 18));
    navigation->setSpacing(3);
    auto* pages = new QStackedWidget(&dialog);
    pages->setObjectName(QStringLiteral("settingsPages"));
    content->addWidget(navigation);
    content->addWidget(pages, 1);
    root->addLayout(content, 1);

    auto add_settings_page = [navigation, pages](const QString& title, QWidget* page)
    {
        auto* item = new QListWidgetItem(title, navigation);
        item->setSizeHint(QSize(156, 42));
        const PlayerIcons::Name icon =
            title == QStringLiteral("常规") ? PlayerIcons::Name::General :
            title == QStringLiteral("播放") ? PlayerIcons::Name::Playback :
            title == QStringLiteral("界面") ? PlayerIcons::Name::Interface :
            title == QStringLiteral("视频显示") ? PlayerIcons::Name::Video :
            title == QStringLiteral("画面滤镜") ? PlayerIcons::Name::Filters :
            title == QStringLiteral("字幕加载") || title == QStringLiteral("字幕样式")
                ? PlayerIcons::Name::Subtitles :
            title == QStringLiteral("快捷键") ? PlayerIcons::Name::Keyboard :
            title == QStringLiteral("截图") ? PlayerIcons::Name::Screenshot :
            PlayerIcons::Name::Performance;
        item->setIcon(PlayerIcons::make(icon));
        page->setObjectName(QStringLiteral("settingsPage"));
        const QString description =
            title == QStringLiteral("常规") ? QStringLiteral("配置文件位置与 Windows 视频文件关联") :
            title == QStringLiteral("播放") ? QStringLiteral("播放行为、默认速度与队列策略") :
            title == QStringLiteral("界面") ? QStringLiteral("窗口行为、鼠标操作与全屏控制") :
            title == QStringLiteral("视频显示") ? QStringLiteral("画面比例与显示适配方式") :
            title == QStringLiteral("画面滤镜") ? QStringLiteral("调整画面色彩与图像处理效果") :
            title == QStringLiteral("字幕加载") ? QStringLiteral("字幕启用、自动匹配与时间同步") :
            title == QStringLiteral("字幕样式") ? QStringLiteral("设置字幕字体、字号、颜色与位置") :
            title == QStringLiteral("快捷键") ? QStringLiteral("查看、搜索并修改播放器快捷键") :
            title == QStringLiteral("截图") ? QStringLiteral("设置截图保存位置与图像格式") :
            QStringLiteral("查看当前视频渲染与解码状态");
        auto* heading = new QLabel(title, page);
        heading->setObjectName(QStringLiteral("settingsPageTitle"));
        auto* summary = new QLabel(description, page);
        summary->setObjectName(QStringLiteral("settingsPageDescription"));
        summary->setWordWrap(true);
        if (auto* page_form = qobject_cast<QFormLayout*>(page->layout()))
        {
            page_form->insertRow(0, heading);
            page_form->insertRow(1, summary);
            page_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            page_form->setFormAlignment(Qt::AlignTop);
            page_form->setHorizontalSpacing(18);
            page_form->setVerticalSpacing(13);
            page_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        }
        else if (auto* page_box = qobject_cast<QBoxLayout*>(page->layout()))
        {
            page_box->insertWidget(0, heading);
            page_box->insertWidget(1, summary);
        }
        if (page->layout())
            page->layout()->setContentsMargins(12, 12, 12, 12);
        if (auto* nested_form = page->findChild<QFormLayout*>())
        {
            nested_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            nested_form->setFormAlignment(Qt::AlignTop);
            nested_form->setHorizontalSpacing(18);
            nested_form->setVerticalSpacing(13);
            nested_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        }
        auto* scroll = new QScrollArea(pages);
        scroll->setObjectName(QStringLiteral("settingsPageScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(page);
        pages->addWidget(scroll);
    };
    connect(navigation, &QListWidget::currentRowChanged,
        pages, &QStackedWidget::setCurrentIndex);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog#settingsDialog { background: #171a20; color: #e8eaf0; }"
        "QWidget { color: #e8eaf0; font-size: 9pt; }"
        "QLabel { background: transparent; }"
        "QLabel#settingsPageTitle { color: #f0f3f8; font-size: 15pt; font-weight: 600; padding-bottom: 2px; }"
        "QLabel#settingsPageDescription { color: #9ba6b5; padding-bottom: 8px; }"
        "QListWidget#settingsNavigation { background: #20252d; border: 1px solid #343c49; "
        "border-radius: 8px; padding: 6px; outline: 0; }"
        "QListWidget#settingsNavigation::item { min-height: 28px; padding: 7px 10px; border-radius: 5px; }"
        "QListWidget#settingsNavigation::item:hover { background: #303846; }"
        "QListWidget#settingsNavigation::item:selected { background: #345b86; color: #ffffff; }"
        "QStackedWidget#settingsPages { background: #1d222a; border: 1px solid #343c49; border-radius: 8px; }"
        "QScrollArea#settingsPageScroll, QWidget#settingsPage { background: #1d222a; border: 0; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QKeySequenceEdit { "
        "background: #252b35; color: #e8eaf0; border: 1px solid #414b5a; "
        "border-radius: 5px; min-height: 28px; padding: 2px 8px; selection-background-color: #345b86; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #5796d0; }"
        "QLineEdit:read-only { color: #aab3c0; background: #20252d; }"
        "QComboBox QAbstractItemView { background: #252b35; color: #e8eaf0; "
        "selection-background-color: #345b86; border: 1px solid #414b5a; }"
        "QPushButton { background: #2a303a; border: 1px solid #414957; "
        "border-radius: 5px; padding: 6px 12px; min-height: 28px; }"
        "QPushButton:hover { background: #384252; border-color: #566579; }"
        "QPushButton:pressed { background: #202630; }"
        "QPushButton:disabled { color: #777f8c; background: #22262d; }"
        "QCheckBox { spacing: 9px; padding: 4px 0; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #596474; "
        "border-radius: 4px; background: #252b35; }"
        "QCheckBox::indicator:checked { background: #3979b7; border-color: #65a4df; }"
        "QSlider::groove:horizontal { height: 4px; background: #424a56; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; background: #e4edf7; "
        "border: 1px solid #6c9bc5; border-radius: 6px; }"
        "QDialogButtonBox { border-top: 1px solid #343c49; padding-top: 10px; }"
        "QScrollBar:vertical { width: 10px; background: #1d222a; margin: 2px; }"
        "QScrollBar::handle:vertical { background: #485363; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: #607087; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QToolTip { background: #252b35; color: #e8eaf0; border: 1px solid #596474; }"));

    auto* general_page = new QWidget(pages);
    auto* general_form = new QFormLayout(general_page);
    auto* settings_path = new QLineEdit(settings_file_path(), general_page);
    settings_path->setReadOnly(true);
    auto* open_settings_directory = new QPushButton(
        QStringLiteral("打开配置目录"), general_page);
    open_settings_directory->setIcon(PlayerIcons::make(PlayerIcons::Name::Folder));
    open_settings_directory->setIconSize(QSize(16, 16));
    connect(open_settings_directory, &QPushButton::clicked, &dialog, []
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(settings_file_path()).absolutePath()));
    });
    general_form->addRow(QStringLiteral("INI 配置文件"), settings_path);
    general_form->addRow(open_settings_directory);
    auto* association_info = new QLabel(
        QStringLiteral("安装版会为当前 Windows 用户注册支持的视频格式，不修改其他用户的关联。"
            "Windows 仍负责默认应用选择；便携版可通过系统“打开方式”选择 LNPlayer。"
            "支持：MP4、MKV、MOV、AVI、WebM、WMV、FLV、MPEG、MPG、M4V、TS、MTS、"
            "M2TS、3GP、3G2、VOB、OGV、MXF、ASF、F4V、DIVX、MPE、MPV。"),
        general_page);
    association_info->setWordWrap(true);
    auto* open_default_apps = new QPushButton(
        QStringLiteral("打开 Windows 默认应用设置"), general_page);
    open_default_apps->setIcon(PlayerIcons::make(PlayerIcons::Name::External));
    open_default_apps->setIconSize(QSize(16, 16));
    connect(open_default_apps, &QPushButton::clicked, &dialog, []
    {
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:defaultapps?registeredAppUser=LNPlayer")));
    });
    general_form->addRow(QStringLiteral("视频文件关联"), association_info);
    general_form->addRow(open_default_apps);
    add_settings_page(QStringLiteral("常规"), general_page);

    auto* playback_page = new QWidget(pages);
    auto* playback_form = new QFormLayout(playback_page);
    auto* remember_position = new QCheckBox(
        QStringLiteral("记住每个视频上次播放位置"), playback_page);
    remember_position->setChecked(remember_position_);
    auto* auto_play_next = new QCheckBox(
        QStringLiteral("当前视频结束后自动播放下一项"), playback_page);
    auto_play_next->setChecked(auto_play_next_);
    auto* default_playback_rate = new QDoubleSpinBox(playback_page);
    default_playback_rate->setRange(0.25, 4.0);
    default_playback_rate->setSingleStep(0.25);
    default_playback_rate->setDecimals(2);
    default_playback_rate->setSuffix(QStringLiteral(" ×"));
    default_playback_rate->setValue(playback_rate_);
    auto* repeat_mode = new QComboBox(playback_page);
    repeat_mode->addItem(QStringLiteral("关闭循环"), 0);
    repeat_mode->addItem(QStringLiteral("列表循环"), 1);
    repeat_mode->addItem(QStringLiteral("单项循环"), 2);
    repeat_mode->setCurrentIndex(repeat_mode_);
    auto* shuffle_enabled = new QCheckBox(QStringLiteral("启用随机播放"), playback_page);
    shuffle_enabled->setChecked(shuffle_enabled_);
    playback_form->addRow(remember_position);
    playback_form->addRow(auto_play_next);
    playback_form->addRow(QStringLiteral("默认播放速度"), default_playback_rate);
    playback_form->addRow(QStringLiteral("循环模式"), repeat_mode);
    playback_form->addRow(shuffle_enabled);
    add_settings_page(QStringLiteral("播放"), playback_page);

    auto* interface_page = new QWidget(pages);
    auto* interface_form = new QFormLayout(interface_page);
    auto* hide_delay = new QSpinBox(interface_page);
    hide_delay->setRange(0, 10);
    hide_delay->setSuffix(QStringLiteral(" 秒"));
    hide_delay->setSpecialValueText(QStringLiteral("关闭"));
    hide_delay->setValue(fullscreen_hide_delay_ms_ / 1000);
    auto* osd_duration = new QSpinBox(interface_page);
    osd_duration->setRange(500, 5000);
    osd_duration->setSingleStep(250);
    osd_duration->setSuffix(QStringLiteral(" ms"));
    osd_duration->setValue(osd_duration_ms_);
    auto* osd_position = new QComboBox(interface_page);
    osd_position->addItem(QStringLiteral("顶部"), 0);
    osd_position->addItem(QStringLiteral("居中"), 1);
    osd_position->addItem(QStringLiteral("底部"), 2);
    osd_position->setCurrentIndex(osd_position_);
    auto* show_playlist = new QCheckBox(QStringLiteral("显示播放列表侧栏"), interface_page);
    show_playlist->setChecked(playlist_panel_visible_);
    auto* always_on_top = new QCheckBox(QStringLiteral("窗口始终置顶"), interface_page);
    always_on_top->setChecked(always_on_top_);
    auto* middle_click = new QComboBox(interface_page);
    middle_click->addItem(QStringLiteral("无动作"), 0);
    middle_click->addItem(QStringLiteral("播放/暂停"), 1);
    middle_click->addItem(QStringLiteral("切换全屏"), 2);
    middle_click->setCurrentIndex(std::max(0, middle_click->findData(middle_click_action_)));
    auto* double_click = new QComboBox(interface_page);
    double_click->addItem(QStringLiteral("无动作"), 2);
    double_click->addItem(QStringLiteral("播放/暂停"), 1);
    double_click->addItem(QStringLiteral("切换全屏"), 0);
    double_click->setCurrentIndex(std::max(0, double_click->findData(double_click_action_)));
    auto* mouse_gestures = new QCheckBox(
        QStringLiteral("启用右键拖动手势（左/右：快退/快进，上/下：音量）"), interface_page);
    mouse_gestures->setChecked(mouse_gestures_enabled_);
    auto* wheel_action = new QComboBox(interface_page);
    wheel_action->addItem(QStringLiteral("调节音量"), 0);
    wheel_action->addItem(QStringLiteral("快退/快进 5 秒"), 1);
    wheel_action->addItem(QStringLiteral("缩放画面"), 2);
    wheel_action->setCurrentIndex(wheel_action_);
    interface_form->addRow(QStringLiteral("播放控制栏自动隐藏"), hide_delay);
    interface_form->addRow(QStringLiteral("OSD 显示时长"), osd_duration);
    interface_form->addRow(QStringLiteral("OSD 位置"), osd_position);
    interface_form->addRow(show_playlist);
    interface_form->addRow(always_on_top);
    interface_form->addRow(QStringLiteral("画面中键操作"), middle_click);
    interface_form->addRow(QStringLiteral("画面双击操作"), double_click);
    interface_form->addRow(QStringLiteral("鼠标滚轮操作"), wheel_action);
    interface_form->addRow(mouse_gestures);
    add_settings_page(QStringLiteral("界面"), interface_page);

    auto* video_page = new QWidget(pages);
    auto* video_form = new QFormLayout(video_page);
    auto* aspect = new QComboBox(video_page);
    aspect->addItem(QStringLiteral("原始比例"), 0);
    aspect->addItem(QStringLiteral("16:9"), 1);
    aspect->addItem(QStringLiteral("4:3"), 2);
    aspect->addItem(QStringLiteral("拉伸填充"), 3);
    aspect->addItem(QStringLiteral("自定义比例"), 4);
    const int aspect_index = video_stretch_ ? 3
        : std::abs(video_aspect_ratio_override_ - 16.0f / 9.0f) < 0.001f ? 1
        : std::abs(video_aspect_ratio_override_ - 4.0f / 3.0f) < 0.001f ? 2
        : video_aspect_ratio_override_ > 0.0f ? 4 : 0;
    aspect->setCurrentIndex(aspect_index);
    auto* custom_aspect = new QDoubleSpinBox(video_page);
    custom_aspect->setRange(0.5, 3.0);
    custom_aspect->setSingleStep(0.01);
    custom_aspect->setDecimals(3);
    custom_aspect->setSuffix(QStringLiteral(" : 1"));
    custom_aspect->setValue(video_aspect_ratio_override_ > 0.0f
        ? video_aspect_ratio_override_ : 16.0 / 9.0);
    custom_aspect->setEnabled(aspect_index == 4);
    connect(aspect, qOverload<int>(&QComboBox::currentIndexChanged),
        custom_aspect, [aspect, custom_aspect](int)
        {
            custom_aspect->setEnabled(aspect->currentData().toInt() == 4);
        });

    auto* filter_page = new QWidget(pages);
    auto* filter_form = new QFormLayout(filter_page);
    auto make_filter_spin = [&filter_page](double value, double minimum, double maximum, double step)
    {
        auto* spin = new QDoubleSpinBox(filter_page);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setValue(value);
        return spin;
    };
    auto* brightness = make_filter_spin(video_brightness_, -1.0, 1.0, 0.05);
    auto* contrast = make_filter_spin(video_contrast_, 0.0, 2.0, 0.05);
    auto* saturation = make_filter_spin(video_saturation_, 0.0, 2.0, 0.05);
    auto* hue = new QSpinBox(filter_page);
    hue->setRange(-180, 180);
    hue->setSingleStep(5);
    hue->setSuffix(QStringLiteral("°"));
    hue->setValue(video_hue_degrees_);
    auto* sharpness = make_filter_spin(video_sharpness_, 0.0, 2.0, 0.05);
    auto* denoise = make_filter_spin(video_denoise_, 0.0, 1.0, 0.05);
    auto* grayscale = new QCheckBox(QStringLiteral("灰度画面"), filter_page);
    grayscale->setChecked(video_grayscale_);
    video_form->addRow(QStringLiteral("画面比例"), aspect);
    video_form->addRow(QStringLiteral("自定义比例"), custom_aspect);
    add_settings_page(QStringLiteral("视频显示"), video_page);
    filter_form->addRow(QStringLiteral("亮度"), brightness);
    filter_form->addRow(QStringLiteral("对比度"), contrast);
    filter_form->addRow(QStringLiteral("饱和度"), saturation);
    filter_form->addRow(QStringLiteral("色调"), hue);
    filter_form->addRow(QStringLiteral("锐化"), sharpness);
    filter_form->addRow(QStringLiteral("降噪"), denoise);
    filter_form->addRow(grayscale);
    add_settings_page(QStringLiteral("画面滤镜"), filter_page);

    auto* subtitle_page = new QWidget(pages);
    auto* subtitle_form = new QFormLayout(subtitle_page);
    auto* subtitle_enabled = new QCheckBox(
        QStringLiteral("启用字幕显示"), subtitle_page);
    subtitle_enabled->setChecked(subtitles_enabled_);
    auto* auto_match_subtitle = new QCheckBox(
        QStringLiteral("自动加载同名外挂字幕"), subtitle_page);
    auto_match_subtitle->setChecked(auto_match_subtitle_);
    auto* subtitle_offset = new QSpinBox(subtitle_page);
    subtitle_offset->setRange(-60000, 60000);
    subtitle_offset->setSingleStep(100);
    subtitle_offset->setSuffix(QStringLiteral(" ms"));
    subtitle_offset->setValue(static_cast<int>(subtitle_offset_ms_));
    auto* subtitle_style_page = new QWidget(pages);
    auto* subtitle_style_form = new QFormLayout(subtitle_style_page);
    auto* subtitle_font = new QFontComboBox(subtitle_style_page);
    subtitle_font->setCurrentFont(QFont(subtitle_font_family_));
    auto* subtitle_size = new QSpinBox(subtitle_style_page);
    subtitle_size->setRange(0, 96);
    subtitle_size->setSpecialValueText(QStringLiteral("自动"));
    subtitle_size->setSuffix(QStringLiteral(" px"));
    subtitle_size->setValue(subtitle_font_size_);
    auto* subtitle_color = new QComboBox(subtitle_style_page);
    subtitle_color->addItem(QStringLiteral("白色"), QStringLiteral("#ffffff"));
    subtitle_color->addItem(QStringLiteral("黄色"), QStringLiteral("#ffff00"));
    subtitle_color->addItem(QStringLiteral("青色"), QStringLiteral("#00ffff"));
    subtitle_color->addItem(QStringLiteral("绿色"), QStringLiteral("#00ff00"));
    int subtitle_color_index = subtitle_color->findData(subtitle_color_);
    subtitle_color->setCurrentIndex(subtitle_color_index >= 0 ? subtitle_color_index : 0);
    auto* subtitle_position = new QSpinBox(subtitle_style_page);
    subtitle_position->setRange(60, 80);
    subtitle_position->setSuffix(QStringLiteral(" %"));
    subtitle_position->setValue(subtitle_vertical_position_);
    subtitle_form->addRow(subtitle_enabled);
    subtitle_form->addRow(auto_match_subtitle);
    subtitle_form->addRow(QStringLiteral("默认时间偏移"), subtitle_offset);
    add_settings_page(QStringLiteral("字幕加载"), subtitle_page);
    subtitle_style_form->addRow(QStringLiteral("字体"), subtitle_font);
    subtitle_style_form->addRow(QStringLiteral("字号"), subtitle_size);
    subtitle_style_form->addRow(QStringLiteral("颜色"), subtitle_color);
    subtitle_style_form->addRow(QStringLiteral("垂直位置"), subtitle_position);
    add_settings_page(QStringLiteral("字幕样式"), subtitle_style_page);

    auto* shortcut_page = new QWidget(pages);
    auto* shortcut_layout = new QVBoxLayout(shortcut_page);
    auto* shortcut_search = new QLineEdit(shortcut_page);
    shortcut_search->setPlaceholderText(QStringLiteral("搜索快捷键名称或按键..."));
    auto* shortcut_form = new QFormLayout();
    QHash<QString, QKeySequenceEdit*> shortcut_edits;
    for (auto it = shortcuts_.cbegin(); it != shortcuts_.cend(); ++it)
    {
        auto* edit = new QKeySequenceEdit(it.value()->key(), shortcut_page);
        shortcut_edits.insert(it.key(), edit);
        shortcut_form->addRow(it.value()->property("commandLabel").toString(), edit);
    }
    auto* shortcut_form_widget = new QWidget(shortcut_page);
    shortcut_form_widget->setLayout(shortcut_form);
    connect(shortcut_search, &QLineEdit::textChanged, shortcut_page,
        [shortcut_form, shortcut_edits](const QString& text)
        {
            const QString query = text.trimmed();
            for (auto it = shortcut_edits.cbegin(); it != shortcut_edits.cend(); ++it)
            {
                auto* label = qobject_cast<QLabel*>(shortcut_form->labelForField(it.value()));
                const QString command = label ? label->text() : QString();
                const QString sequence = it.value()->keySequence().toString(
                    QKeySequence::NativeText);
                const bool visible = query.isEmpty() ||
                    command.contains(query, Qt::CaseInsensitive) ||
                    sequence.contains(query, Qt::CaseInsensitive);
                if (label)
                    label->setVisible(visible);
                it.value()->setVisible(visible);
            }
        });
    auto* shortcut_scroll = new QScrollArea(shortcut_page);
    shortcut_scroll->setWidgetResizable(true);
    shortcut_scroll->setWidget(shortcut_form_widget);
    shortcut_layout->addWidget(shortcut_search);
    shortcut_layout->addWidget(shortcut_scroll);
    add_settings_page(QStringLiteral("快捷键"), shortcut_page);

    auto* screenshot_page = new QWidget(pages);
    auto* screenshot_form = new QFormLayout(screenshot_page);
    auto* screenshot_directory = new QLineEdit(screenshot_directory_, screenshot_page);
    auto* browse_screenshot_directory = new QPushButton(
        QStringLiteral("浏览..."), screenshot_page);
    browse_screenshot_directory->setIcon(PlayerIcons::make(PlayerIcons::Name::Folder));
    browse_screenshot_directory->setIconSize(QSize(16, 16));
    auto* screenshot_directory_row = new QWidget(screenshot_page);
    auto* screenshot_directory_layout = new QHBoxLayout(screenshot_directory_row);
    screenshot_directory_layout->setContentsMargins(0, 0, 0, 0);
    screenshot_directory_layout->addWidget(screenshot_directory, 1);
    screenshot_directory_layout->addWidget(browse_screenshot_directory);
    connect(browse_screenshot_directory, &QPushButton::clicked, &dialog,
        [&, screenshot_directory]
    {
        const QString directory = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("选择截图保存目录"),
            screenshot_directory->text().isEmpty()
                ? QDir(application_data_directory()).filePath(QStringLiteral("Screenshots"))
                : screenshot_directory->text());
        if (!directory.isEmpty())
            screenshot_directory->setText(directory);
    });
    auto* screenshot_format = new QComboBox(screenshot_page);
    screenshot_format->addItem(QStringLiteral("PNG（无损）"), QStringLiteral("png"));
    screenshot_format->addItem(QStringLiteral("JPEG（压缩）"), QStringLiteral("jpg"));
    screenshot_format->addItem(QStringLiteral("BMP（无压缩）"), QStringLiteral("bmp"));
    const int screenshot_format_index = screenshot_format->findData(screenshot_format_);
    screenshot_format->setCurrentIndex(screenshot_format_index >= 0
        ? screenshot_format_index : 0);
    screenshot_form->addRow(QStringLiteral("默认保存目录"), screenshot_directory_row);
    screenshot_form->addRow(QStringLiteral("默认图片格式"), screenshot_format);
    add_settings_page(QStringLiteral("截图"), screenshot_page);

    auto* performance_page = new QWidget(pages);
    auto* performance_form = new QFormLayout(performance_page);
    const QString renderer_description = video_surface_ && video_surface_->has_renderer()
        ? QStringLiteral("D3D11 renderer 已初始化")
        : QStringLiteral("D3D11 renderer 未初始化");
    performance_form->addRow(QStringLiteral("渲染器"), new QLabel(renderer_description, performance_page));
    performance_form->addRow(QStringLiteral("视频分辨率"),
        new QLabel(video_width_ > 0
            ? QStringLiteral("%1 × %2").arg(video_width_).arg(video_height_)
            : QStringLiteral("尚未取得首帧"), performance_page));
    performance_form->addRow(QStringLiteral("说明"),
        new QLabel(QStringLiteral("解码器和硬件加速由 Core/FFmpeg 根据媒体与设备自动选择。"), performance_page));
    add_settings_page(QStringLiteral("性能"), performance_page);

    navigation->setCurrentRow(0);

    auto* reset_scope = new QLabel(
        QStringLiteral("恢复默认仅重置播放器偏好设置；不会删除播放列表、书签、最近播放记录或各视频的播放位置。"),
        &dialog);
    reset_scope->setWordWrap(true);
    reset_scope->setStyleSheet(QStringLiteral("color: #9ba6b5;"));
    root->addWidget(reset_scope);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* defaults = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    defaults->setIcon(PlayerIcons::make(PlayerIcons::Name::Reset));
    defaults->setIconSize(QSize(16, 16));
    root->addWidget(buttons);

    connect(defaults, &QPushButton::clicked, &dialog, [=]
    {
        remember_position->setChecked(true);
        auto_play_next->setChecked(true);
        default_playback_rate->setValue(1.0);
        repeat_mode->setCurrentIndex(0);
        shuffle_enabled->setChecked(false);
        hide_delay->setValue(2);
        osd_duration->setValue(1500);
        osd_position->setCurrentIndex(1);
        show_playlist->setChecked(true);
        always_on_top->setChecked(false);
        middle_click->setCurrentIndex(middle_click->findData(2));
        double_click->setCurrentIndex(double_click->findData(1));
        wheel_action->setCurrentIndex(0);
        mouse_gestures->setChecked(false);
        aspect->setCurrentIndex(0);
        custom_aspect->setValue(16.0 / 9.0);
        brightness->setValue(0.0);
        contrast->setValue(1.0);
        saturation->setValue(1.0);
        hue->setValue(0);
        sharpness->setValue(0.0);
        denoise->setValue(0.0);
        grayscale->setChecked(false);
        subtitle_enabled->setChecked(true);
        auto_match_subtitle->setChecked(true);
        subtitle_offset->setValue(0);
        subtitle_font->setCurrentFont(QFont(QStringLiteral("Segoe UI")));
        subtitle_size->setValue(0);
        subtitle_color->setCurrentIndex(0);
        subtitle_position->setValue(80);
        screenshot_directory->setText(QDir(application_data_directory()).filePath(
            QStringLiteral("Screenshots")));
        screenshot_format->setCurrentIndex(0);
        for (auto it = shortcuts_.cbegin(); it != shortcuts_.cend(); ++it)
        {
            const QKeySequence default_sequence = QKeySequence::fromString(
                it.value()->property("defaultSequence").toString());
            shortcut_edits.value(it.key())->setKeySequence(default_sequence);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this]
    {
        QList<QPair<QString, QKeySequence>> candidates;
        for (auto it = shortcut_edits.cbegin(); it != shortcut_edits.cend(); ++it)
        {
            const QKeySequence sequence = it.value()->keySequence();
            if (sequence.isEmpty())
                continue;
            for (const auto& candidate : candidates)
            {
                if (candidate.second.matches(sequence) != QKeySequence::NoMatch ||
                    sequence.matches(candidate.second) != QKeySequence::NoMatch)
                {
                    QMessageBox::warning(&dialog, QStringLiteral("快捷键冲突"),
                        QStringLiteral("快捷键与“%1”重复，请修改后再保存。")
                            .arg(shortcuts_.value(candidate.first)->property("commandLabel").toString()));
                    return;
                }
            }
            candidates.append(qMakePair(it.key(), sequence));
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const bool fullscreen_before_dialog = isFullScreen();
    if (fullscreen_before_dialog)
        fullscreen_hide_timer_->stop();
    const int dialog_result = dialog.exec();
    if (fullscreen_before_dialog && isFullScreen())
    if (dialog_result != QDialog::Accepted)
        return;

    remember_position_ = remember_position->isChecked();
    auto_play_next_ = auto_play_next->isChecked();
    repeat_mode_ = repeat_mode->currentData().toInt();
    shuffle_enabled_ = shuffle_enabled->isChecked();
    wheel_action_ = wheel_action->currentData().toInt();
    if (std::abs(playback_rate_ - default_playback_rate->value()) > 0.001)
        setPlaybackRate(static_cast<float>(default_playback_rate->value()));
    fullscreen_hide_delay_ms_ = hide_delay->value() * 1000;
    osd_duration_ms_ = osd_duration->value();
    osd_position_ = osd_position->currentData().toInt();
    fullscreen_hide_timer_->setInterval(fullscreen_hide_delay_ms_);
    playlist_panel_visible_ = show_playlist->isChecked();
    always_on_top_ = always_on_top->isChecked();
    middle_click_action_ = middle_click->currentData().toInt();
    double_click_action_ = double_click->currentData().toInt();
    mouse_gestures_enabled_ = mouse_gestures->isChecked();
    if (video_surface_)
        video_surface_->set_gesture_enabled(mouse_gestures_enabled_);
    if (shuffle_button_)
    {
        shuffle_button_->setChecked(shuffle_enabled_);
        shuffle_button_->setIcon(PlayerIcons::make(PlayerIcons::Name::Shuffle,
            shuffle_enabled_ ? QColor(QStringLiteral("#d7dde7"))
                : QColor(QStringLiteral("#8f99a8"))));
        shuffle_button_->setToolTip(shuffle_enabled_
            ? QStringLiteral("随机播放：开") : QStringLiteral("随机播放：关"));
        shuffle_button_->setAccessibleName(shuffle_button_->toolTip());
    }
    if (repeat_button_)
    {
        repeat_button_->setIcon(PlayerIcons::make(
            repeat_mode_ == 2 ? PlayerIcons::Name::RepeatOne : PlayerIcons::Name::Repeat,
            repeat_mode_ == 0 ? QColor(QStringLiteral("#8f99a8"))
                : QColor(QStringLiteral("#d7dde7"))));
        repeat_button_->setToolTip(repeat_mode_ == 0 ? QStringLiteral("循环播放：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环播放：列表") : QStringLiteral("循环播放：单项"));
        repeat_button_->setAccessibleName(repeat_button_->toolTip());
    }
    resetShuffleOrder();
    saveVideoPlaylist();
    if (playlist_visibility_action_)
        playlist_visibility_action_->setChecked(playlist_panel_visible_);
    if (always_on_top_action_)
        always_on_top_action_->setChecked(always_on_top_);
    if (playlist_panel_ && !playlist_panel_visible_)
        playlist_panel_->hide();
    const bool was_fullscreen = isFullScreen();
    const bool was_maximized = isMaximized();
    setWindowFlag(Qt::WindowStaysOnTopHint, always_on_top_);
    if (was_fullscreen)
        showFullScreen();
    else if (was_maximized)
        showMaximized();
    else
        show();

    const int selected_aspect = aspect->currentData().toInt();
    video_stretch_ = selected_aspect == 3;
    video_aspect_ratio_override_ = selected_aspect == 1 ? 16.0f / 9.0f
        : selected_aspect == 2 ? 4.0f / 3.0f
        : selected_aspect == 4 ? static_cast<float>(custom_aspect->value())
        : 0.0f;
    video_brightness_ = static_cast<float>(brightness->value());
    video_contrast_ = static_cast<float>(contrast->value());
    video_saturation_ = static_cast<float>(saturation->value());
    video_hue_degrees_ = hue->value();
    video_sharpness_ = static_cast<float>(sharpness->value());
    video_denoise_ = static_cast<float>(denoise->value());
    video_grayscale_ = grayscale->isChecked();
    subtitles_enabled_ = subtitle_enabled->isChecked();
    auto_match_subtitle_ = auto_match_subtitle->isChecked();
    subtitle_offset_ms_ = subtitle_offset->value();
    subtitle_font_family_ = subtitle_font->currentFont().family();
    subtitle_font_size_ = subtitle_size->value();
    subtitle_color_ = subtitle_color->currentData().toString();
    subtitle_vertical_position_ = subtitle_position->value();
    screenshot_directory_ = screenshot_directory->text().trimmed().isEmpty()
        ? QDir(application_data_directory()).filePath(QStringLiteral("Screenshots"))
        : screenshot_directory->text().trimmed();
    screenshot_format_ = screenshot_format->currentData().toString();
    video_surface_->set_aspect_ratio(video_aspect_ratio_override_);
    video_surface_->set_stretch(video_stretch_);
    video_surface_->set_video_filter(
        video_brightness_, video_contrast_, video_saturation_,
        video_hue_degrees_, video_sharpness_, video_denoise_, video_grayscale_);

    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Video/Playback/RememberPosition"), remember_position_);
    settings.setValue(QStringLiteral("Video/Playback/AutoPlayNext"), auto_play_next_);
    settings.setValue(QStringLiteral("Video/Window/AlwaysOnTop"), always_on_top_);
    settings.setValue(QStringLiteral("Video/UI/FullscreenHideDelayMs"), fullscreen_hide_delay_ms_);
    settings.setValue(QStringLiteral("Video/UI/OSDDurationMs"), osd_duration_ms_);
    settings.setValue(QStringLiteral("Video/UI/OSDPosition"), osd_position_);
    for (auto it = shortcuts_.begin(); it != shortcuts_.end(); ++it)
    {
        const QKeySequence sequence = shortcut_edits.value(it.key())->keySequence();
        it.value()->setKey(sequence);
        settings.setValue(
            QStringLiteral("Video/Shortcuts/") + it.key(),
            sequence.toString(QKeySequence::PortableText));
    }
    settings.setValue(QStringLiteral("Video/UI/PlaylistVisible"), playlist_panel_visible_);
    settings.setValue(QStringLiteral("Video/Mouse/MiddleClickAction"), middle_click_action_);
    settings.setValue(QStringLiteral("Video/Mouse/DoubleClickAction"), double_click_action_);
    settings.setValue(QStringLiteral("Video/Mouse/WheelAction"), wheel_action_);
    settings.setValue(QStringLiteral("Video/Mouse/RightDragGestures"), mouse_gestures_enabled_);
    settings.setValue(QStringLiteral("Video/Display/AspectRatio"), video_aspect_ratio_override_);
    settings.setValue(QStringLiteral("Video/Display/Stretch"), video_stretch_);
    settings.setValue(QStringLiteral("Video/Filter/Brightness"), video_brightness_);
    settings.setValue(QStringLiteral("Video/Filter/Contrast"), video_contrast_);
    settings.setValue(QStringLiteral("Video/Filter/Saturation"), video_saturation_);
    settings.setValue(QStringLiteral("Video/Filter/HueDegrees"), video_hue_degrees_);
    settings.setValue(QStringLiteral("Video/Filter/Sharpness"), video_sharpness_);
    settings.setValue(QStringLiteral("Video/Filter/Denoise"), video_denoise_);
    settings.setValue(QStringLiteral("Video/Filter/Grayscale"), video_grayscale_);
    settings.setValue(QStringLiteral("Video/Subtitle/Enabled"), subtitles_enabled_);
    settings.setValue(QStringLiteral("Video/Subtitle/AutoMatch"), auto_match_subtitle_);
    settings.setValue(QStringLiteral("Video/Subtitle/OffsetMs"), subtitle_offset_ms_);
    settings.setValue(QStringLiteral("Video/Subtitle/FontFamily"), subtitle_font_family_);
    settings.setValue(QStringLiteral("Video/Subtitle/FontSize"), subtitle_font_size_);
    settings.setValue(QStringLiteral("Video/Subtitle/Color"), subtitle_color_);
    settings.setValue(QStringLiteral("Video/Subtitle/VerticalPosition"), subtitle_vertical_position_);
    settings.setValue(QStringLiteral("Video/Screenshot/Directory"), screenshot_directory_);
    settings.setValue(QStringLiteral("Video/Screenshot/Format"), screenshot_format_);
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        QMessageBox::warning(this, QStringLiteral("设置保存失败"),
            QStringLiteral("设置已应用到当前会话，但无法写入配置文件：\n%1")
                .arg(settings_file_path()));
    }

    setSubtitleOffset(subtitle_offset_ms_);
    postCommand([this, enabled = subtitles_enabled_]
    {
        if (player_)
            lnplayer_set_subtitles_enabled(player_, enabled ? 1 : 0);
    });
    pending_video_frame_valid_ = false;
    requestVideoFrame();
    if (isFullScreen())
    {
    }
}
