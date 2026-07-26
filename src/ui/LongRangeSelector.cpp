#include "LongRangeSelector.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "utils/MathUtils.h"
#include "model/Chart.h"
#include "model/Note.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QRegularExpression>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QEvent>
#include <cmath>
#include <algorithm>

// --- 静态工具方法 ---

std::optional<LongRangeSelector::BeatValue> LongRangeSelector::parseBeat(const QString &text)
{
    // 匹配格式：整数 分子/分母（中间以空格分隔，分母前有斜杠）
    static const QRegularExpression re(R"(^(-?\d+)\s+(\d+)\s*/\s*(\d+)$)");
    QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return std::nullopt;

    bool ok1, ok2, ok3;
    int integer = m.captured(1).toInt(&ok1);
    int num = m.captured(2).toInt(&ok2);
    int den = m.captured(3).toInt(&ok3);

    if (!ok1 || !ok2 || !ok3)
        return std::nullopt;

    return BeatValue{integer, num, den};
}

bool LongRangeSelector::isValidBeat(const BeatValue &bv)
{
    return isValidBeat(bv.integer, bv.numerator, bv.denominator);
}

bool LongRangeSelector::isValidBeat(int integer, int numerator, int denominator)
{
    Q_UNUSED(integer);
    return denominator > 0 && numerator >= 0 && numerator < denominator;
}

QString LongRangeSelector::formatBeat(const BeatValue &bv)
{
    return formatBeat(bv.integer, bv.numerator, bv.denominator);
}

QString LongRangeSelector::formatBeat(int integer, int numerator, int denominator)
{
    return QString("%1 %2/%3").arg(integer).arg(numerator).arg(denominator);
}

double LongRangeSelector::beatToDouble(const BeatValue &bv)
{
    return beatToDouble(bv.integer, bv.numerator, bv.denominator);
}

double LongRangeSelector::beatToDouble(int integer, int numerator, int denominator)
{
    if (denominator <= 0)
        return static_cast<double>(integer);
    return static_cast<double>(integer) + static_cast<double>(numerator) / static_cast<double>(denominator);
}

// --- 构造与 UI 搭建 ---

LongRangeSelector::LongRangeSelector(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    m_undoMergeTimer = new QTimer(this);
    m_undoMergeTimer->setSingleShot(true);
    m_undoMergeTimer->setInterval(500);
    connect(m_undoMergeTimer, &QTimer::timeout, this, &LongRangeSelector::onUndoMergeTimeout);
}

void LongRangeSelector::setupUi()
{
    QGroupBox *group = new QGroupBox(tr("Range Select"), this);
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(group);

    QVBoxLayout *layout = new QVBoxLayout(group);

    // 起始 beat 行
    QHBoxLayout *startRow = new QHBoxLayout;
    QLabel *startLabel = new QLabel(tr("Start:"), group);
    m_startEdit = new QLineEdit(group);
    m_startEdit->setPlaceholderText(QStringLiteral("0 0/4"));
    m_startEdit->setText(QStringLiteral("0 0/4"));
    m_startNowBtn = new QPushButton(tr("Now"), group);
    m_startNowBtn->setFixedWidth(48);
    startRow->addWidget(startLabel);
    startRow->addWidget(m_startEdit, 1);
    startRow->addWidget(m_startNowBtn);
    layout->addLayout(startRow);

    // 结束 beat 行
    QHBoxLayout *endRow = new QHBoxLayout;
    QLabel *endLabel = new QLabel(tr("End:"), group);
    m_endEdit = new QLineEdit(group);
    m_endEdit->setPlaceholderText(QStringLiteral("0 0/4"));
    m_endEdit->setText(QStringLiteral("0 0/4"));
    m_endNowBtn = new QPushButton(tr("Now"), group);
    m_endNowBtn->setFixedWidth(48);
    endRow->addWidget(endLabel);
    endRow->addWidget(m_endEdit, 1);
    endRow->addWidget(m_endNowBtn);
    layout->addLayout(endRow);

    // 选择按钮
    m_selectBtn = new QPushButton(tr("Select"), group);
    layout->addWidget(m_selectBtn);

    // 连接信号
    connect(m_startEdit, &QLineEdit::textChanged, this, &LongRangeSelector::onStartTextChanged);
    connect(m_endEdit, &QLineEdit::textChanged, this, &LongRangeSelector::onEndTextChanged);
    connect(m_startNowBtn, &QPushButton::clicked, this, &LongRangeSelector::onStartNowClicked);
    connect(m_endNowBtn, &QPushButton::clicked, this, &LongRangeSelector::onEndNowClicked);
    connect(m_selectBtn, &QPushButton::clicked, this, &LongRangeSelector::onSelectClicked);

    // 安装事件过滤器以捕获滚轮事件
    m_startEdit->installEventFilter(this);
    m_endEdit->installEventFilter(this);

    updateValidationState();
}

// --- 控件设置 ---

void LongRangeSelector::setChartController(ChartController *controller)
{
    m_chartController = controller;
}

void LongRangeSelector::setSelectionController(SelectionController *controller)
{
    m_selectionController = controller;
}

void LongRangeSelector::setPlaybackController(PlaybackController *controller)
{
    m_playbackController = controller;
}

void LongRangeSelector::setTimeDivision(int division)
{
    if (division < 1)
        division = 1;
    m_timeDivision = division;
}

void LongRangeSelector::retranslateUi()
{
    // 通过查找子控件重新设置文本
    if (auto *g = findChild<QGroupBox *>())
        g->setTitle(tr("Range Select"));
    // 遍历 label 重新设置
    const auto labels = findChildren<QLabel *>();
    for (auto *l : labels)
    {
        if (l->text().startsWith("Start") || l->text().startsWith("Start") || l->text() == "Start:")
            l->setText(tr("Start:"));
        else if (l->text().startsWith("End") || l->text() == "End:")
            l->setText(tr("End:"));
    }
    if (m_startNowBtn)
        m_startNowBtn->setText(tr("Now"));
    if (m_endNowBtn)
        m_endNowBtn->setText(tr("Now"));
    if (m_selectBtn)
        m_selectBtn->setText(tr("Select"));
}

// --- 验证 ---

void LongRangeSelector::onStartTextChanged(const QString &text)
{
    Q_UNUSED(text);
    updateValidationState();
}

void LongRangeSelector::onEndTextChanged(const QString &text)
{
    Q_UNUSED(text);
    updateValidationState();
}

void LongRangeSelector::updateValidationState()
{
    auto startParsed = parseBeat(m_startEdit->text());
    auto endParsed = parseBeat(m_endEdit->text());

    bool startValid = startParsed.has_value() && isValidBeat(*startParsed);
    bool endValid = endParsed.has_value() && isValidBeat(*endParsed);

    static const QString invalidStyle = QStringLiteral("QLineEdit { background: #ffcccc; }");
    static const QString validStyle = QStringLiteral("");

    m_startEdit->setStyleSheet(startValid ? validStyle : invalidStyle);
    m_endEdit->setStyleSheet(endValid ? validStyle : invalidStyle);

    m_selectBtn->setEnabled(startValid && endValid);
}

// --- 自动交换 ---

void LongRangeSelector::autoSwapIfNeeded()
{
    auto startParsed = parseBeat(m_startEdit->text());
    auto endParsed = parseBeat(m_endEdit->text());

    if (!startParsed.has_value() || !endParsed.has_value())
        return;
    if (!isValidBeat(*startParsed) || !isValidBeat(*endParsed))
        return;

    double startVal = beatToDouble(*startParsed);
    double endVal = beatToDouble(*endParsed);

    if (startVal > endVal)
    {
        // 交换文本
        QString tmp = m_startEdit->text();
        m_startEdit->blockSignals(true);
        m_endEdit->blockSignals(true);
        m_startEdit->setText(m_endEdit->text());
        m_endEdit->setText(tmp);
        m_startEdit->blockSignals(false);
        m_endEdit->blockSignals(false);
        // 恢复验证状态
        updateValidationState();
    }
}

// --- 当前时间填入 ---

void LongRangeSelector::onStartNowClicked()
{
    fillCurrentTime(m_startEdit);
}

void LongRangeSelector::onEndNowClicked()
{
    fillCurrentTime(m_endEdit);
}

void LongRangeSelector::fillCurrentTime(QLineEdit *edit)
{
    if (!m_playbackController || !m_chartController)
        return;

    double timeMs = m_playbackController->currentTime();

    const Chart *chart = m_chartController->chart();
    if (!chart)
        return;

    int outBeatNum, outNumerator, outDenominator;
    MathUtils::msToBeat(timeMs, chart->bpmList(), chart->meta().offset,
                        outBeatNum, outNumerator, outDenominator);

    // 用量化到当前分度，遇 .5 向较早时间取整（floor）
    double floatBeat = beatToDouble(outBeatNum, outNumerator, outDenominator);
    double snapped = std::floor(floatBeat * m_timeDivision) / m_timeDivision;

    // 转换回分数形式
    int snappedBeatNum, snappedNumerator, snappedDenominator;
    MathUtils::floatToBeat(snapped, snappedBeatNum, snappedNumerator, snappedDenominator, m_timeDivision);

    // 强制使用当前分度作为分母
    int num = static_cast<int>(std::round((snapped - snappedBeatNum) * m_timeDivision));
    if (num < 0)
    {
        num += m_timeDivision;
        snappedBeatNum -= 1;
    }
    if (num >= m_timeDivision)
    {
        num -= m_timeDivision;
        snappedBeatNum += 1;
    }

    // 保存撤销状态
    saveUndoState(edit);

    edit->setText(formatBeat(snappedBeatNum, num, m_timeDivision));
}

// --- 选择行为 ---

void LongRangeSelector::onSelectClicked()
{
    autoSwapIfNeeded();
    performSelection();
}

void LongRangeSelector::performSelection()
{
    if (!m_chartController || !m_selectionController)
        return;

    auto startParsed = parseBeat(m_startEdit->text());
    auto endParsed = parseBeat(m_endEdit->text());

    if (!startParsed.has_value() || !endParsed.has_value())
        return;
    if (!isValidBeat(*startParsed) || !isValidBeat(*endParsed))
        return;

    double startBeat = beatToDouble(*startParsed);
    double endBeat = beatToDouble(*endParsed);

    const Chart *chart = m_chartController->chart();
    if (!chart)
        return;

    const QVector<Note> &notes = chart->notes();
    QSet<int> selectedIndices;

    for (int i = 0; i < notes.size(); ++i)
    {
        const Note &note = notes[i];
        double noteStart = note.getStartBeat();

        if (note.isRain)
        {
            // 长 note：要求头尾完整包含于区间内
            double noteEnd = note.getEndBeat();
            if (noteStart >= startBeat && noteEnd <= endBeat)
                selectedIndices.insert(i);
        }
        else
        {
            // 普通 note：击中时间在区间内
            if (noteStart >= startBeat && noteStart <= endBeat)
                selectedIndices.insert(i);
        }
    }

    m_selectionController->select(selectedIndices);
}

// --- 滚轮调整 ---

bool LongRangeSelector::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Wheel)
    {
        QWheelEvent *wheel = static_cast<QWheelEvent *>(event);
        QLineEdit *edit = qobject_cast<QLineEdit *>(obj);
        if (edit && (edit == m_startEdit || edit == m_endEdit))
        {
            // 检查鼠标是否在该输入框上方
            QPoint localPos = edit->mapFromGlobal(wheel->globalPosition().toPoint());
            if (edit->rect().contains(localPos))
            {
                int delta = wheel->angleDelta().y();
                bool shiftHeld = (wheel->modifiers() & Qt::ShiftModifier) != 0;

                if (delta != 0)
                {
                    adjustBeat(edit, delta, shiftHeld);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void LongRangeSelector::adjustBeat(QLineEdit *edit, int delta, bool shiftHeld)
{
    int direction = (delta > 0) ? 1 : -1;

    auto parsed = parseBeat(edit->text());
    int integer, numerator, denominator;

    if (parsed.has_value() && isValidBeat(*parsed))
    {
        integer = parsed->integer;
        numerator = parsed->numerator;
        denominator = parsed->denominator;
    }
    else
    {
        // 非法格式或假分数，从 0 0/当前分度 开始
        integer = 0;
        numerator = 0;
        denominator = m_timeDivision;
    }

    // 保存撤销状态
    saveUndoState(edit);

    if (shiftHeld)
    {
        // Shift+滚动：整数部分 ±1，分数不变
        integer += direction;
    }
    else
    {
        // 普通滚动：分子 ±1，自动进位/退位
        numerator += direction;

        // 处理进位
        while (numerator >= denominator)
        {
            numerator -= denominator;
            integer += 1;
        }
        // 处理退位
        while (numerator < 0)
        {
            numerator += denominator;
            integer -= 1;
        }
    }

    edit->setText(formatBeat(integer, numerator, denominator));
}

// --- 撤销合并 ---

void LongRangeSelector::saveUndoState(QLineEdit *edit)
{
    if (!m_undoMergeActive)
    {
        // 首次修改：保存两个框的当前值
        m_savedStartText = m_startEdit->text();
        m_savedEndText = m_endEdit->text();
        m_undoMergeActive = true;
    }
    // 重启定时器
    m_undoMergeTimer->start();
    m_lastAdjustedEdit = edit;
}

void LongRangeSelector::onUndoMergeTimeout()
{
    // 定时器超时，合并窗口关闭。下次修改将创建新的撤销点。
    m_undoMergeActive = false;
    m_lastAdjustedEdit = nullptr;
}

void LongRangeSelector::tryTriggerUndo(QLineEdit *edit)
{
    if (!m_undoMergeActive)
        return;

    // 恢复两个框到保存的值
    m_undoMergeTimer->stop();

    m_startEdit->blockSignals(true);
    m_endEdit->blockSignals(true);

    m_startEdit->setText(m_savedStartText);
    m_endEdit->setText(m_savedEndText);

    m_startEdit->blockSignals(false);
    m_endEdit->blockSignals(false);

    m_undoMergeActive = false;
    m_lastAdjustedEdit = nullptr;

    updateValidationState();
}