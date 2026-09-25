#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/limiter/Ui.hpp>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

static bool check(bool ok,const char* message){if(!ok)std::fprintf(stderr,"FAIL: %s\n",message);return ok;}
int main(int argc,char** argv){
    QApplication app(argc,argv);bool pass=true;
    WinJACKNexus::UI::LimiterWidget widget;
    pass&=check(widget.size()==QSize(768,650),"reference window size");
    QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    {QPainter painter(&image);widget.render(&painter);}
    pass&=check(image.pixelColor(25,25)!=image.pixelColor(25,175),"separate channel strip and effect panel");
    if(argc>2)pass&=check(image.save(QString::fromLocal8Bit(argv[2])),"preview saved");
    int changes=0;unsigned mask=0;std::array<WinJACKNexus::UI::LimiterParameters,8> changedValues{};
    widget.setChangeHandler([&](std::size_t i,const auto& value){++changes;mask|=1u<<i;changedValues[i]=value;});
    auto click=[&](QPointF p){QMouseEvent e(QEvent::MouseButtonPress,p,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&e);};
    click({681,604});pass&=check(changes==8&&mask==255,"All edits every channel");
    std::array<WinJACKNexus::UI::LimiterParameters,8> initial{};
    for(std::size_t i=0;i<initial.size();++i)initial[i].thresholdDb=-static_cast<float>(i);
    widget.setParameters(initial);changes=0;mask=0;
    click({591,301});
    {QMouseEvent e(QEvent::MouseMove,{591,291},{591,291},Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&e);}
    {QMouseEvent e(QEvent::MouseButtonRelease,{591,291},{591,291},Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&widget,&e);}
    pass&=check(changes==8&&mask==255,"All dial edit broadcasts to every channel");
    for(const auto& value:changedValues)pass&=check(std::abs(value.thresholdDb+3.0F)<0.001F,"All dial edit overwrites each channel with the same absolute value");
    changes=0;mask=0;
    click({132,85});changes=0;mask=0;click({681,604});
    pass&=check(widget.selectedChannel()==0&&changes==1&&mask==1,"single channel keeps independent settings");
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
            pass&=check(count&&name&&category&&create&&destroy&&get&&set,"node exports");
            if(count&&name&&category&&create&&destroy&&get&&set){
                pass&=check(count()==1&&std::strcmp(name(0),"Limiter")==0&&std::strcmp(category(0),"Native Fx")==0,"Limiter discoverable");
                auto* node=create(0);pass&=check(node!=nullptr,"Limiter node creates");if(node){
                    pass&=check(node->createWindow(nullptr)!=nullptr,"Limiter opens its standalone window");
                    node->destroyWindow(nullptr);
                    QByteArray json(get(node));pass&=check(QJsonDocument::fromJson(json).object().value("channels").toArray().size()==8,"eight parameter sets");
                    pass&=check(set(node,json.constData())==1,"settings round trip");
                    pass&=check(set(node,"{\"channels\":[]}")==0,"invalid settings rejected");destroy(node);
                }
            }FreeLibrary(module);
        }
    }
    return pass?0:1;
}
