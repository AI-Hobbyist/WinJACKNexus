#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/delay/Ui.hpp>
#include <WinJACKNexus/NativeFx/delay/Processor.hpp>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

static bool check(bool ok,const char* message){if(!ok)std::fprintf(stderr,"FAIL: %s\n",message);return ok;}
int main(int argc,char** argv){
    QApplication app(argc,argv);bool pass=true;
    {
        using WinJACKNexus::Common::DelayParameters;
        using WinJACKNexus::Common::DelayProcessor;
        DelayParameters p;p.timeMs=10;p.feedback=0;p.wetDb=0;p.dryDb=-60;p.highPass=false;p.lowPass=false;
        DelayProcessor processor;processor.setSampleRate(8000);
        float input[200]{};float output[200]{};input[0]=1;
        processor.process(input,output,200,p);
        pass&=check(output[0]>0&&output[0]<.002F&&output[80]>.99F&&output[79]==0&&output[81]==0,
                    "delay emits one impulse at the configured sample offset");
        p.sync=true;p.note=2;p.bpm=120;p.dotted=false;
        pass&=check(DelayProcessor::delayMilliseconds(p)==500,"quarter note at 120 BPM is 500 ms");
        p.dotted=true;pass&=check(DelayProcessor::delayMilliseconds(p)==750,"dotted note multiplies time by 1.5");
        p.enabled=false;processor.process(input,output,200,p);
        pass&=check(output[0]==1&&output[80]==0,"bypass passes dry input unchanged");
    }
    WinJACKNexus::UI::DelayWidget widget;
    pass&=check(widget.size()==QSize(750,645),"reference size with channel strip");
    QMouseEvent dragPress(QEvent::MouseButtonPress,QPointF(64,526),QPointF(64,526),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dragRelease(QEvent::MouseButtonRelease,QPointF(64,526),QPointF(64,526),
                            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&dragPress);
    auto* dragRefresh=widget.findChild<QTimer*>(QStringLiteral("nativeFxDragRefreshTimer"));
    pass&=check(dragRefresh&&dragRefresh->isActive(),"Delay dial drag uses the application frame timer");
    QApplication::sendEvent(&widget,&dragRelease);
    pass&=check(dragRefresh&&!dragRefresh->isActive(),"Delay dial drag timer stops on release");
    QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    {QPainter painter(&image);widget.render(&painter);}
    pass&=check(image.pixelColor(25,25)!=image.pixelColor(25,240),"channel strip and panel are separate");
    if(argc>2)pass&=check(image.save(QString::fromLocal8Bit(argv[2])),"preview saved");
    int changes=0;unsigned mask=0;std::array<WinJACKNexus::UI::DelayParameters,8> changed{};
    widget.setChangeHandler([&](std::size_t i,const auto& value){++changes;mask|=1u<<i;changed[i]=value;});
    auto click=[&](QPointF p){QMouseEvent e(QEvent::MouseButtonPress,p,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&e);};
    click({350,180});pass&=check(changes==8&&mask==255,"All toggle broadcasts to eight channels");
    std::array<WinJACKNexus::UI::DelayParameters,8> initial{};
    for(std::size_t i=0;i<8;++i){initial[i].feedback=float(10+i*8);initial[i].enabled=(i%2==0);}
    widget.setParameters(initial);changes=0;mask=0;
    click({350,180});
    pass&=check(changes==8&&mask==255,"All toggle broadcasts an absolute state");
    for(const auto& value:changed)pass&=check(!value.enabled,"All toggle overwrites prior per-channel states");
    changes=0;mask=0;
    QWheelEvent wheel(QPointF(64,526),QPointF(64,526),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(&widget,&wheel);
    pass&=check(changes==8&&mask==255,"All dial broadcasts to eight channels");
    for(const auto& value:changed)pass&=check(value.feedback==11,"All dial writes one absolute target");
    changes=0;mask=0;
    click({64,570});
    auto* editor=widget.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
    pass&=check(editor!=nullptr,"Delay feedback readout opens a precise value editor");
    if(editor){
        editor->setText(QStringLiteral("42"));
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(editor,&enter);
    }
    pass&=check(changes==8&&mask==255,"Delay All numeric edit reaches eight channels");
    for(const auto& value:changed)pass&=check(value.feedback==42,"Delay All numeric edit writes one absolute target");
    changes=0;mask=0;
    QMouseEvent reset(QEvent::MouseButtonDblClick,QPointF(64,526),QPointF(64,526),
                      Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&reset);
    pass&=check(changes==8&&mask==255,"Delay double click resets All channels");
    for(const auto& value:changed)pass&=check(value.feedback==WinJACKNexus::UI::DelayParameters{}.feedback,
                                                "Delay double click restores feedback default");
    std::array<WinJACKNexus::UI::DelayParameters,8> synced{};
    for(auto& value:synced){value.sync=true;value.note=2;value.bpm=120;}
    widget.setParameters(synced);changes=0;mask=0;
    click({170,408});
    editor=nullptr;
    for(auto* candidate:widget.findChildren<QLineEdit*>(QStringLiteral("numericValueEditor")))
        if(!candidate->isHidden())editor=candidate;
    pass&=check(editor!=nullptr,"Delay synced time readout opens an editor");
    if(editor){
        editor->setText(QStringLiteral("600"));
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(editor,&enter);
    }
    pass&=check(changes==8&&mask==255,"Delay synced time edit reaches eight channels");
    for(const auto& value:changed)
        pass&=check(value.sync&&std::abs(value.bpm-100.0F)<0.01F,
                    "Delay synced millisecond entry maps to an absolute BPM target");
    pass&=check(widget.hostPresetIndex()==6,"edited Delay parameters are labeled Custom");
    changes=0;mask=0;
    widget.chooseHostPreset(0);
    pass&=check(changes==8&&mask==255&&widget.hostPresetIndex()==0,
                "Delay Default preset restores all selected channels");
    for(const auto& value:changed)
        pass&=check(value.feedback==WinJACKNexus::UI::DelayParameters{}.feedback&&!value.sync,
                    "Delay Default restores actual default parameters");
    click({135,85});changes=0;mask=0;click({350,180});
    pass&=check(changes==1&&mask==1,"single channel edit stays independent");
    if(argc>1){auto* module=LoadLibraryW(QString::fromLocal8Bit(argv[1]).toStdWString().c_str());
        pass&=check(module!=nullptr,"native module loads");if(module){
            using Count=std::uint32_t(*)();using Name=const char*(*)(std::uint32_t);
            using Create=WinJACKNexus::Node::INode*(*)(std::uint32_t);using Destroy=void(*)(WinJACKNexus::Node::INode*);
            using Get=const char*(*)(WinJACKNexus::Node::INode*);using Set=int(*)(WinJACKNexus::Node::INode*,const char*);
            auto count=reinterpret_cast<Count>(GetProcAddress(module,"wjn_get_node_count"));
            auto name=reinterpret_cast<Name>(GetProcAddress(module,"wjn_get_node_name"));
            auto category=reinterpret_cast<Name>(GetProcAddress(module,"wjn_get_node_category"));
            auto create=reinterpret_cast<Create>(GetProcAddress(module,"wjn_create_node"));
            auto destroy=reinterpret_cast<Destroy>(GetProcAddress(module,"wjn_destroy_node"));
            auto get=reinterpret_cast<Get>(GetProcAddress(module,"wjn_get_node_settings_json"));
            auto set=reinterpret_cast<Set>(GetProcAddress(module,"wjn_set_node_settings_json"));
            using PresetName=const char*(*)(std::uint32_t);
            auto presetName=reinterpret_cast<PresetName>(GetProcAddress(module,"wjn_fx_preset_name"));
            pass&=check(count&&name&&category&&create&&destroy&&get&&set,"node exports");
            pass&=check(presetName&&std::strcmp(presetName(0),"Default")==0,
                        "Delay mixer API labels its default preset Default");
            if(count&&name&&category&&create&&destroy&&get&&set){
                pass&=check(count()==1&&std::strcmp(name(0),"Delay")==0&&std::strcmp(category(0),"Native Fx")==0,"Delay discoverable");
                auto* node=create(0);pass&=check(node!=nullptr,"Delay node creates");if(node){
                    pass&=check(node->createWindow(nullptr)!=nullptr,"standalone window opens");node->destroyWindow(nullptr);
                    QByteArray json(get(node));pass&=check(QJsonDocument::fromJson(json).object().value("channels").toArray().size()==8,"eight parameter sets");
                    pass&=check(set(node,json.constData())==1,"settings round trip");
                    pass&=check(set(node,"{\"channels\":[]}")==0,"invalid settings rejected");destroy(node);
                }
            }FreeLibrary(module);
        }
    }
    return pass?0:1;
}
