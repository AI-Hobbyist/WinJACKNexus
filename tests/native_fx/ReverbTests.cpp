#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/reverb/Ui.hpp>
#include <WinJACKNexus/NativeFx/reverb/Processor.hpp>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QLineEdit>
#include <QKeyEvent>
#include <QFileInfo>
#include <QDir>
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static bool check(bool ok,const char* message){if(!ok)std::fprintf(stderr,"FAIL: %s\n",message);return ok;}
int main(int argc,char** argv){
    QApplication app(argc,argv);bool pass=true;
    using WinJACKNexus::Common::ReverbParameters;
    using WinJACKNexus::Common::ReverbProcessor;
    ReverbProcessor processor;processor.setSampleRate(8000);
    ReverbParameters parameters;parameters.wet=100;parameters.dryDb=-60;parameters.preDelayMs=0;
    std::array<float,16000> input{},output{};input[0]=1;
    processor.process(input.data(),output.data(),unsigned(input.size()),parameters);
    float tail=0;for(int i=250;i<12000;++i){pass&=check(std::isfinite(output[i]),"reverb output stays finite");tail+=std::abs(output[i]);}
    pass&=check(tail>.1F,"impulse produces an audible reverb tail");
    parameters.enabled=false;processor.process(input.data(),output.data(),256,parameters);
    pass&=check(output[0]==1&&output[1]==0,"bypass passes input unchanged");
    WinJACKNexus::UI::ReverbWidget widget;
    pass&=check(widget.size()==QSize(770,645),"reference-width standalone interface with channel strip");
    QMouseEvent dragPress(QEvent::MouseButtonPress,QPointF(185,433),QPointF(185,433),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dragRelease(QEvent::MouseButtonRelease,QPointF(185,433),QPointF(185,433),
                            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&dragPress);
    auto* dragRefresh=widget.findChild<QTimer*>(QStringLiteral("nativeFxDragRefreshTimer"));
    pass&=check(dragRefresh&&dragRefresh->isActive(),"Reverb dial drag uses the application frame timer");
    QApplication::sendEvent(&widget,&dragRelease);
    pass&=check(dragRefresh&&!dragRefresh->isActive(),"Reverb dial drag timer stops on release");
    pass&=check(widget.hostPresetIndex()==0 && widget.selectedScene()==1,
                "Reverb starts with Default preset in the Hall scene");
    QImage preview(widget.size(),QImage::Format_ARGB32_Premultiplied);preview.fill(Qt::transparent);
    {QPainter painter(&preview);widget.render(&painter);}
    pass&=check(preview.pixelColor(25,20)!=preview.pixelColor(25,175),"channel selector and effect panel are distinct");
    if(argc>2)pass&=check(preview.save(QString::fromLocal8Bit(argv[2])),"preview saved");
    unsigned mask=0;std::array<ReverbParameters,8> changed{};
    widget.setChangeHandler([&](std::size_t i,const auto& p){mask|=1u<<i;changed[i]=p;});
    auto click=[&](QPointF point){QMouseEvent e(QEvent::MouseButtonPress,point,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&e);};
    click({185,470});
    auto* numeric=widget.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
    pass&=check(numeric!=nullptr,"Reverb readout opens the shared numeric editor");
    if(numeric){numeric->setText(QStringLiteral("3.5"));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(numeric,&enter);}
    pass&=check(mask==255&&std::all_of(changed.begin(),changed.end(),[](const auto& p){return p.decay==3.5F;}),
                "Reverb All numeric edit applies one absolute decay target");
    QMouseEvent reset(QEvent::MouseButtonDblClick,QPointF(185,434),QPointF(185,434),
                      QPointF(185,434),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&reset);
    pass&=check(std::all_of(changed.begin(),changed.end(),[](const auto& p){return p.decay==2.4F;}),
                "Reverb double-click restores the shared default value");
    click({75,143+135});pass&=check(widget.hostPresetIndex()==1&&widget.selectedScene()==0,
        "Room scene selects Small Room after preset reorder");
    click({75,143+174});pass&=check(widget.hostPresetIndex()==0&&widget.selectedScene()==1,
        "Hall scene selects Default after preset reorder");
    click({75,143+369});pass&=check(mask==255&&widget.selectedScene()==6,"Custom scene broadcasts in All mode");
    std::array<ReverbParameters,8> independent{};
    for(int i=0;i<8;++i){independent[i].scene=6;independent[i].decay=float(i+1);independent[i].size=float(60+i);}
    widget.setParameters(independent);
    QImage customA(widget.size(),QImage::Format_ARGB32_Premultiplied);{QPainter p(&customA);widget.render(&p);}
    QWheelEvent wheel(QPointF(280,433),QPointF(280,433),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    mask=0;QApplication::sendEvent(&widget,&wheel);
    pass&=check(mask==255,"All size edit reaches every channel");
    for(const auto& p:changed)pass&=check(p.size==64.5F,"All writes one absolute size target from the displayed mean");
    for(int i=0;i<8;++i)pass&=check(changed[i].decay==float(i+1),"All edit preserves unedited per-channel decay");
    QImage customB(widget.size(),QImage::Format_ARGB32_Premultiplied);{QPainter p(&customB);widget.render(&p);}
    pass&=check(customA.copy(142,236,383,156)!=customB.copy(142,236,383,156),"Custom cylinder changes with room size");
    if(argc>2)pass&=check(customB.save(QFileInfo(QString::fromLocal8Bit(argv[2])).dir().filePath(QStringLiteral("Reverb-custom-preview.png"))),"Custom preview saved");
    click({140,88});mask=0;click({75,143+330});
    pass&=check(mask==1&&widget.selectedChannel()==0,"single channel scene edit is independent");
    if(argc>1){auto* module=LoadLibraryW(QString::fromLocal8Bit(argv[1]).toStdWString().c_str());
        pass&=check(module!=nullptr,"Native Fx module loads");if(module){
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
            pass&=check(count&&name&&category&&create&&destroy&&get&&set,"node exports");
            if(count&&name&&category&&create&&destroy&&get&&set){
                pass&=check(count()==1&&std::strcmp(name(0),"Reverb")==0&&std::strcmp(category(0),"Native Fx")==0,"Reverb node discoverable");
                auto* node=create(0);pass&=check(node!=nullptr,"Reverb node creates");if(node){
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
