/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/delay/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace WinJACKNexus::UI {
namespace {
const QColor cyan(43,204,255), pale(213,229,255), muted(155,181,220);
constexpr const char* channels[]={"All","L","R","C","LFE","Ls","Rs","Lb","Rb"};
void box(QPainter& p,QRectF r,qreal radius,QColor fill,QColor edge={}) {
    p.setPen(edge.isValid()?QPen(edge,1):Qt::NoPen);p.setBrush(fill);p.drawRoundedRect(r,radius,radius);
}
void label(QPainter& p,QRectF r,const QString& text,int size,QColor color,bool bold=false) {
    QFont font(QStringLiteral("Microsoft YaHei UI"),size);font.setBold(bold);p.setFont(font);p.setPen(color);p.drawText(r,Qt::AlignCenter,text);
}
void chevron(QPainter& p,QPointF c) {p.setPen(QPen(muted,1.3));p.drawLine(c+QPointF(-4,-2),c+QPointF(0,2));p.drawLine(c+QPointF(0,2),c+QPointF(4,-2));}
void toggle(QPainter& p,QRectF r,bool on) {
    box(p,r,r.height()/2,on?QColor(13,141,238):QColor(42,64,88));
    p.setPen(Qt::NoPen);p.setBrush(Qt::white);p.drawEllipse(QPointF(on?r.right()-r.height()/2:r.left()+r.height()/2,r.center().y()),r.height()*.36,r.height()*.36);
}
void styleCombo(QComboBox* combo) {
    combo->setStyleSheet(QStringLiteral(
        "QComboBox { background:#142639; color:#d5e5ff; border:1px solid #3f5c77; "
        "border-radius:5px; padding:2px 7px; }"
        "QComboBox:hover { border-color:#2bccff; }"
        "QComboBox::drop-down { border:0; width:23px; }"
        "QComboBox QLineEdit { border:0; background:transparent; color:#d5e5ff; padding:0; }"
        "QComboBox QAbstractItemView { background:#172b40; color:#d5e5ff; "
        "selection-background-color:#0d4164; selection-color:#ffffff; "
        "border:1px solid #3f5c77; outline:0; }"));
    combo->setMaxVisibleItems(12);
    combo->setFocusPolicy(Qt::StrongFocus);
    combo->hide();
}
void drawSelect(QPainter& p,QRectF r,const QString& value) {
    box(p,r,5,QColor(18,36,55),QColor(60,86,111));
    label(p,{r.x()+7,r.y(),r.width()-29,r.height()},value,12,pale);
    chevron(p,{r.right()-15,r.center().y()});
}
void styleMenu(QMenu& menu) {
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #10263a; color: #e1edf8; border: 1px solid #315775; padding: 4px; }"
        "QMenu::item { padding: 7px 22px; }"
        "QMenu::item:selected { background: #075478; color: #56d9f4; }"
        "QMenu::item:checked { color: #56d9f4; }"));
}
void addComboItem(QComboBox* combo,const QString& text,const QVariant& data={}) {
    const int index=combo->count();
    combo->addItem(text,data);
    combo->setItemData(index,Qt::AlignCenter,Qt::TextAlignmentRole);
}
void dial(QPainter& p,QPoint c,int radius,float amount) {
    p.setPen(QPen(QColor(5,13,24),5));p.setBrush(QColor(20,38,57));p.drawEllipse(c,radius-4,radius-4);
    QRectF arc(c.x()-radius,c.y()-radius,2*radius,2*radius);
    p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(39,67,91),3));p.drawArc(arc,225*16,-270*16);
    p.setPen(QPen(cyan,3,Qt::SolidLine,Qt::RoundCap));p.drawArc(arc,225*16,-int(270*amount*16));
    const float angle=(225-270*amount)*3.14159265F/180;
    p.setPen(QPen(QColor(139,224,255),2,Qt::SolidLine,Qt::RoundCap));
    p.drawLine(QPointF(c),QPointF(c.x()+std::cos(angle)*(radius-15),c.y()-std::sin(angle)*(radius-15)));
}
}
DelayWidget::DelayWidget(QWidget* parent):QWidget(parent) {
    setFixedSize(750,645);setMouseTracking(true);setWindowTitle(QStringLiteral("Delay 延迟"));
    presetCombo_=new QComboBox(this);presetCombo_->setGeometry(455,168,160,34);styleCombo(presetCombo_);
    presetCombo_->addItems({QStringLiteral("自定义"),QStringLiteral("人声回声"),QStringLiteral("短 slap"),QStringLiteral("节奏回声"),QStringLiteral("氛围长回声"),QStringLiteral("磁带回声")});
    noteCombo_=new QComboBox(this);noteCombo_->setGeometry(202,263,82,34);styleCombo(noteCombo_);
    for(const auto& text:{QStringLiteral("1/16"),QStringLiteral("1/8"),QStringLiteral("1/4"),QStringLiteral("1/2"),QStringLiteral("1")})addComboItem(noteCombo_,text);
    viewCombo_=new QComboBox(this);viewCombo_->setGeometry(599,244,120,35);styleCombo(viewCombo_);
    addComboItem(viewCombo_,QStringLiteral("时间轴"));addComboItem(viewCombo_,QStringLiteral("回声衰减"));
    highPassCombo_=new QComboBox(this);highPassCombo_->setGeometry(406,545,92,30);styleCombo(highPassCombo_);
    for(int hz:{100,150,200,300,400,600,800,1000,1200,1600,2000})addComboItem(highPassCombo_,QStringLiteral("%1 Hz").arg(hz),hz);
    lowPassCombo_=new QComboBox(this);lowPassCombo_->setGeometry(406,582,92,30);styleCombo(lowPassCombo_);
    for(int hz:{2000,3000,4000,5000,6000,8000,10000,12000,16000,20000})addComboItem(lowPassCombo_,QStringLiteral("%1 kHz").arg(hz/1000.0,0,'f',1),hz);
    for(auto* combo:{highPassCombo_,lowPassCombo_}){combo->setEditable(true);combo->lineEdit()->setReadOnly(true);combo->lineEdit()->setFrame(false);}
    mixCombo_=new QComboBox(this);mixCombo_->setGeometry(628,560,90,35);styleCombo(mixCombo_);
    addComboItem(mixCombo_,QStringLiteral("正常"));addComboItem(mixCombo_,QStringLiteral("叠加"));
    mixCombo_->setEditable(true);mixCombo_->lineEdit()->setReadOnly(true);mixCombo_->lineEdit()->setAlignment(Qt::AlignCenter);mixCombo_->lineEdit()->setFrame(false);

    connect(presetCombo_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(index<=0)return;
        const DelayParameters presets[]={
            {},
            {.timeMs=360,.feedback=30,.wetDb=-9,.dryDb=0,.highPassHz=180,.lowPassHz=7000,.bpm=120,.sync=true},
            {.timeMs=115,.feedback=14,.wetDb=-12,.dryDb=0,.highPassHz=150,.lowPassHz=10000},
            {.timeMs=500,.feedback=42,.wetDb=-8,.dryDb=0,.highPassHz=200,.lowPassHz=8000,.bpm=120,.sync=true},
            {.timeMs=820,.feedback=68,.wetDb=-15,.dryDb=-2,.highPassHz=220,.lowPassHz=6200},
            {.timeMs=420,.feedback=48,.wetDb=-8,.dryDb=0,.highPassHz=250,.lowPassHz=5200,.bpm=120,.sync=true,.dotted=true}
        };
        const auto preset=presets[std::clamp(index,1,5)];
        for(int i=channel_<0?0:channel_;i<(channel_<0?8:channel_+1);++i){values_[i]=preset;if(handler_)handler_(i,values_[i]);}
        preset_=index;syncControls();update();
    });
    connect(noteCombo_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index>=0)edit([&](auto& v){v.note=index;});});
    connect(viewCombo_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index>=0){view_=index;update();}});
    connect(highPassCombo_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index>=0){const float hz=highPassCombo_->itemData(index).toFloat();edit([&](auto& v){v.highPassHz=hz;});}});
    connect(lowPassCombo_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index>=0){const float hz=lowPassCombo_->itemData(index).toFloat();edit([&](auto& v){v.lowPassHz=hz;});}});
    connect(mixCombo_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index>=0)edit([&](auto& v){v.mixMode=index;});});
    for(auto* combo:{presetCombo_,noteCombo_,viewCombo_,highPassCombo_,lowPassCombo_,mixCombo_})combo->hide();
    syncControls();
}
void DelayWidget::setParameters(const std::array<DelayParameters,8>& values) {values_=values;preset_=0;syncControls();update();}
void DelayWidget::setChangeHandler(ChangeHandler handler) {handler_=std::move(handler);}
void DelayWidget::edit(const std::function<void(DelayParameters&)>& change) {
    preset_=0;
    const auto before=values_[channel_<0?0:channel_];auto target=before;change(target);
    for(int i=channel_<0?0:channel_;i<(channel_<0?8:channel_+1);++i){
        auto& v=values_[i];
#define APPLY_DELAY_FIELD(field) if(target.field!=before.field)v.field=target.field
        APPLY_DELAY_FIELD(timeMs);APPLY_DELAY_FIELD(feedback);APPLY_DELAY_FIELD(wetDb);APPLY_DELAY_FIELD(dryDb);
        APPLY_DELAY_FIELD(highPassHz);APPLY_DELAY_FIELD(lowPassHz);APPLY_DELAY_FIELD(bpm);
        APPLY_DELAY_FIELD(enabled);APPLY_DELAY_FIELD(sync);APPLY_DELAY_FIELD(dotted);
        APPLY_DELAY_FIELD(highPass);APPLY_DELAY_FIELD(lowPass);APPLY_DELAY_FIELD(note);APPLY_DELAY_FIELD(mixMode);
#undef APPLY_DELAY_FIELD
        if(handler_)handler_(i,v);
    }
    syncControls();update();
}
void DelayWidget::syncControls() {
    const auto& v=values_[channel_<0?0:channel_];
    const QSignalBlocker pBlock(presetCombo_),nBlock(noteCombo_),vBlock(viewCombo_),hBlock(highPassCombo_),lBlock(lowPassCombo_),mBlock(mixCombo_);
    presetCombo_->setCurrentIndex(std::clamp(preset_,0,presetCombo_->count()-1));
    noteCombo_->setCurrentIndex(std::clamp(v.note,0,noteCombo_->count()-1));
    viewCombo_->setCurrentIndex(std::clamp(view_,0,viewCombo_->count()-1));
    auto selectCutoff=[this](QComboBox* combo,float hz){
        int best=0;float distance=std::numeric_limits<float>::max();
        for(int i=0;i<combo->count();++i){float d=std::abs(combo->itemData(i).toFloat()-hz);if(d<distance){distance=d;best=i;}}
        combo->setCurrentIndex(best);
        combo->lineEdit()->setText(combo==highPassCombo_?QStringLiteral("%1 Hz").arg(hz,0,'f',0):QStringLiteral("%1 kHz").arg(hz/1000.0F,0,'f',1));
    };
    selectCutoff(highPassCombo_,v.highPassHz);selectCutoff(lowPassCombo_,v.lowPassHz);
    mixCombo_->setCurrentIndex(std::clamp(v.mixMode,0,1));
}
int DelayWidget::dialAt(const QPoint& point) const {
    const QPoint actual[]={{170,355},{64,526},{168,526},{273,526}};
    for(int i=0;i<4;++i)if(QPointF(point-actual[i]).manhattanLength()<55)return i;
    return -1;
}
void DelayWidget::adjust(int index,float steps) {
    const auto& current=values_[channel_<0?0:channel_];
    if(index==0){if(current.sync){const float target=std::clamp(current.bpm+steps,30.0F,300.0F);edit([&](auto& v){v.bpm=target;});}
        else{const float target=std::clamp(current.timeMs+steps*10.0F,1.0F,2000.0F);edit([&](auto& v){v.timeMs=target;});}}
    if(index==1){const float target=std::clamp(current.feedback+steps,0.0F,95.0F);edit([&](auto& v){v.feedback=target;});}
    if(index==2){const float target=std::clamp(current.wetDb+steps*.5F,-60.0F,6.0F);edit([&](auto& v){v.wetDb=target;});}
    if(index==3){const float target=std::clamp(current.dryDb+steps*.5F,-60.0F,6.0F);edit([&](auto& v){v.dryDb=target;});}
}
void DelayWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(),Controls::themeColor(*this,"fxBackground",QColor(12,26,43)));
    box(p,{7,7,736,126},9,QColor(21,39,59),QColor(45,75,107));
    label(p,{20,17,130,36},QStringLiteral("作用声道"),18,Qt::white,true);
    for(int i=0;i<9;++i){int x=20+i*79;box(p,{qreal(x),65,70,43},6,i==channel_+1?QColor(13,65,100):QColor(24,42,61),i==channel_+1?cyan:QColor(60,86,111));label(p,{qreal(x),65,70,43},QString::fromLatin1(channels[i]),14,i==channel_+1?cyan:pale);}
    box(p,{3,141,744,499},10,QColor(16,33,52),QColor(18,157,231));
    box(p,{12,151,726,68},8,QColor(22,42,62));
    box(p,{14,153,54,54},5,QColor(11,147,190),QColor(23,194,235));
    p.setPen(QPen(Qt::white,2));p.setBrush(Qt::NoBrush);p.drawEllipse(QPointF(41,180),17,17);p.drawLine(QPointF(41,180),QPointF(41,168));p.drawLine(QPointF(41,180),QPointF(51,183));
    label(p,{77,156,155,34},QStringLiteral("Delay 延迟"),20,Qt::white,true);
    label(p,{78,187,142,23},QStringLiteral("创造回声与空间感"),12,cyan);
    toggle(p,{340,171,37,21},values_[channel_<0?0:channel_].enabled);
    label(p,{402,169,48,24},QStringLiteral("预设:"),12,muted);
    drawSelect(p,{455,168,160,34},presetCombo_->currentText());
    for(int i=0;i<3;++i){const int x=624+i*35;
        box(p,{qreal(x),168,29,34},5,QColor(24,43,64),QColor(61,90,119));
        label(p,{qreal(x),168,29,34},i==2?QStringLiteral("•••"):i==0?QStringLiteral("‹"):QStringLiteral("›"),17,muted);
    }
    box(p,{12,220,726,408},8,QColor(14,29,46),QColor(43,69,94));
    label(p,{26,231,70,25},QStringLiteral("时间模式:"),12,pale);
    for(int i=0;i<2;++i){int x=28+i*78;box(p,{qreal(x),263,76,34},5,values_[channel_<0?0:channel_].sync==bool(i)?QColor(13,61,92):QColor(22,42,63),values_[channel_<0?0:channel_].sync==bool(i)?cyan:QColor(55,83,112));label(p,{qreal(x),263,76,34},i?QStringLiteral("同步"):QStringLiteral("自由"),12,pale);}
    label(p,{202,231,60,25},QStringLiteral("音符值:"),12,pale);
    drawSelect(p,{202,263,82,34},noteCombo_->currentText());
    const auto& v=values_[channel_<0?0:channel_];const QString notes[]={QStringLiteral("1/16"),QStringLiteral("1/8"),QStringLiteral("1/4"),QStringLiteral("1/2"),QStringLiteral("1")};
    label(p,{298,266,48,27},QStringLiteral("点音符"),11,pale);toggle(p,{347,269,38,21},v.dotted);
    label(p,{550,246,47,30},QStringLiteral("视图:"),12,pale);
    drawSelect(p,{599,244,120,35},viewCombo_->currentText());
    dial(p,{170,355},38,v.sync?std::clamp((v.bpm-30.0F)/270.0F,0.0F,1.0F):std::clamp(v.timeMs/2000.0F,0.0F,1.0F));
    const float shownMs=WinJACKNexus::Common::DelayProcessor::delayMilliseconds(v);
    label(p,{111,397,118,24},QStringLiteral("%1 ms").arg(shownMs,0,'f',0),13,Qt::white);
    label(p,{98,421,144,40},v.sync?QStringLiteral("延迟时间 · %1 BPM").arg(v.bpm,0,'f',0):QStringLiteral("延迟时间 (Time)"),12,pale);
    for(int i=1;i<4;++i){int x=i==1?64:i==2?168:273;float n=i==1?v.feedback/95.0F:(i==2?v.wetDb:v.dryDb)+60.0F; if(i!=1)n/=66.0F;dial(p,{x,526},30,n);QString t=i==1?QStringLiteral("%1 %").arg(v.feedback,0,'f',0):QStringLiteral("%1 dB").arg(i==2?v.wetDb:v.dryDb,0,'f',1);label(p,{qreal(x-46),559,92,22},t,12,Qt::white);}
    label(p,{25,583,80,20},QStringLiteral("反馈"),12,pale);label(p,{125,583,86,20},QStringLiteral("湿信号"),12,pale);label(p,{231,583,86,20},QStringLiteral("干信号"),12,pale);
    label(p,{25,603,80,21},QStringLiteral("(Feedback)"),10,pale);label(p,{125,603,86,21},QStringLiteral("(Wet)"),10,pale);label(p,{231,603,86,21},QStringLiteral("(Dry)"),10,pale);
    box(p,{334,301,388,206},6,QColor(9,25,40),QColor(49,77,105));
    p.save();p.setClipRect(QRectF(340,307,375,161));
    for(int i=0;i<9;++i){int x=341+i*44;p.setPen(QPen(QColor(41,68,87),.7));p.drawLine(x,302,x,466);}
    for(int i=0;i<4;++i){int y=322+i*39;p.setPen(QPen(QColor(35,62,85),.8));p.drawLine(339,y,716,y);}
    const float scale=350.0F/2000.0F;
    const int baseline=view_?450:382;
    p.setPen(QPen(view_?QColor(69,107,137):cyan,1));p.drawLine(342,baseline,714,baseline);
    for(int tap=1;tap<=16;++tap){
        const int x=341+int(shownMs*tap*scale);if(x>711)break;
        const float amplitude=std::pow(v.feedback/100.0F,tap-1);
        if(amplitude<.012F)break;
        const int height=view_?int(120*amplitude):std::max(3,int(28*amplitude));
        p.setPen(QPen(tap==1?cyan:QColor(149,94,227),view_?4:6,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(x,baseline-(view_?height:height/2),x,baseline+(view_?0:height/2));
    }
    p.restore();
    label(p,{347,344,22,25},channel_<0?QStringLiteral("All"):QString::fromLatin1(channels[channel_+1]),11,pale);
    for(int i=0;i<7;++i)label(p,{qreal(337+i*47),469,47,29},QString::number(i*250),9,muted);
    label(p,{682,469,38,29},QStringLiteral("(ms)"),9,muted);
    box(p,{334,508,190,119},7,QColor(20,40,60),QColor(40,69,93));
    label(p,{344,513,76,24},QStringLiteral("滤波器:"),12,pale);
    for(int i=0;i<2;++i){int y=548+i*37;box(p,{347,qreal(y),19,19},10,QColor(12,40,61),QColor(60,92,120));p.setPen(Qt::NoPen);p.setBrush(i?v.lowPass?cyan:muted:v.highPass?cyan:muted);p.drawEllipse(QPointF(356,y+9),5,5);label(p,{371,qreal(y-3),37,26},i?QStringLiteral("低通"):QStringLiteral("高通"),10,muted);}
    drawSelect(p,{406,545,92,30},highPassCombo_->lineEdit()->text());
    drawSelect(p,{406,582,92,30},lowPassCombo_->lineEdit()->text());
    box(p,{524,508,198,119},7,QColor(20,40,60),QColor(40,69,93));
    label(p,{570,522,106,26},QStringLiteral("混合模式:"),12,pale);
    drawSelect(p,{570,560,106,35},mixCombo_->currentText());
}
void DelayWidget::mousePressEvent(QMouseEvent* e) {
    const QPoint q=e->pos();if(q.y()>=65&&q.y()<108&&q.x()>=20&&q.x()<731){channel_=std::clamp((q.x()-20)/79,0,8)-1;syncControls();update();return;}
    auto showChoices=[this](QComboBox* combo,QPoint origin){
        QMenu menu(this);styleMenu(menu);
        for(int i=0;i<combo->count();++i){auto* action=menu.addAction(combo->itemText(i));
            action->setCheckable(true);action->setChecked(i==combo->currentIndex());
            connect(action,&QAction::triggered,this,[combo,i]{combo->setCurrentIndex(i);});
        }
        menu.exec(mapToGlobal(origin));
    };
    if(QRect(455,168,160,34).contains(q)){showChoices(presetCombo_,{455,202});return;}
    if(q.y()>=168&&q.y()<202&&q.x()>=624&&q.x()<723){
        const int button=(q.x()-624)/35;
        if(button<2){const int count=presetCombo_->count()-1;
            const int current=std::max(1,presetCombo_->currentIndex());
            presetCombo_->setCurrentIndex(1+(current-1+(button==0?count-1:1))%count);
        }else{
            QMenu menu(this);styleMenu(menu);
            auto reset=[this](bool all){
                const int first=all||channel_<0?0:channel_;
                const int end=all||channel_<0?8:channel_+1;
                for(int i=first;i<end;++i){
                    values_[i]=DelayParameters{};
                    if(handler_)handler_(i,values_[i]);
                }
                preset_=0;syncControls();update();
            };
            auto* current=menu.addAction(QStringLiteral("重置当前声道"));
            auto* every=menu.addAction(QStringLiteral("重置所有声道"));
            connect(current,&QAction::triggered,this,[reset]{reset(false);});
            connect(every,&QAction::triggered,this,[reset]{reset(true);});
            menu.exec(mapToGlobal(QPoint(705,202)));
        }return;
    }
    if(QRect(340,171,37,21).contains(q)){edit([](auto& v){v.enabled=!v.enabled;});return;}
    if(q.y()>=263&&q.y()<297&&q.x()>=28&&q.x()<182){bool sync=q.x()>=106;edit([&](auto& v){v.sync=sync;});return;}
    if(QRect(202,263,82,34).contains(q)){showChoices(noteCombo_,{202,297});return;}
    if(QRect(347,269,38,21).contains(q)){edit([](auto& v){v.dotted=!v.dotted;});return;}
    if(QRect(599,244,120,35).contains(q)){showChoices(viewCombo_,{599,279});return;}
    if(QRect(347,548,19,56).contains(q)){int index=q.y()<576?0:1;edit([&](auto& v){if(index)v.lowPass=!v.lowPass;else v.highPass=!v.highPass;});return;}
    if(QRect(406,545,92,30).contains(q)){showChoices(highPassCombo_,{406,575});return;}
    if(QRect(406,582,92,30).contains(q)){showChoices(lowPassCombo_,{406,612});return;}
    if(QRect(570,560,106,35).contains(q)){showChoices(mixCombo_,{570,595});return;}
    dragged_=dialAt(q);dragStart_=q;if(dragged_>=0)dragValue_=values_[channel_<0?0:channel_];
}
void DelayWidget::mouseMoveEvent(QMouseEvent* e) {if(dragged_>=0&&e->buttons()&Qt::LeftButton){float steps=(dragStart_.y()-e->pos().y())*.4F;dragStart_=e->pos();adjust(dragged_,steps);}}
void DelayWidget::mouseReleaseEvent(QMouseEvent*) {dragged_=-1;}
void DelayWidget::wheelEvent(QWheelEvent* e){int index=dialAt(e->position().toPoint());if(index>=0){adjust(index,e->angleDelta().y()/120.0F);e->accept();}else QWidget::wheelEvent(e);}
}
