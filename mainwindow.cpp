/*
 MainWindow Implementation
 Copyright (c)2011 Mark Watkins <jedimark@users.sourceforge.net>
 License: GPL
*/

#include <QGLFormat>
#include <QFileDialog>
#include <QMessageBox>
#include <QResource>
#include <QProgressBar>
#include <QWebHistory>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QSettings>
#include <QPixmap>
#include <QDesktopWidget>
#include <QListView>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QProcess>
#include <cmath>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "newprofile.h"
#include "exportcsv.h"
#include "SleepLib/schema.h"
#include "Graphs/glcommon.h"

QProgressBar *qprogress;
QLabel *qstatus;
QLabel *qstatus2;
QStatusBar *qstatusbar;

extern Profile * profile;

void MainWindow::Log(QString s)
{

    if (!strlock.tryLock())
        return;

//  strlock.lock();
    QString tmp=QString("%1: %2").arg(logtime.elapsed(),5,10,QChar('0')).arg(s);

    logbuffer.append(tmp); //QStringList appears not to be threadsafe
    strlock.unlock();

    strlock.lock();
    // only do this in the main thread?
    for (int i=0;i<logbuffer.size();i++)
        ui->logText->appendPlainText(logbuffer[i]);
    logbuffer.clear();
    strlock.unlock();

    //loglock.unlock();

}


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    Q_ASSERT(p_profile!=NULL);

    logtime.start();
    ui->setupUi(this);
    QString version=PREF["VersionString"].toString();
    if (QString(GIT_BRANCH)!="master") version+=QString(" ")+QString(GIT_BRANCH);
    this->setWindowTitle(tr("SleepyHead")+QString(" v%1 (Profile: %2)").arg(version).arg(PREF["Profile"].toString()));
    ui->tabWidget->setCurrentIndex(0);

    overview=NULL;
    daily=NULL;
    oximetry=NULL;
    prefdialog=NULL;

    qstatusbar=ui->statusbar;
    qprogress=new QProgressBar(this);
    qprogress->setMaximum(100);
    qstatus2=new QLabel("Welcome",this);
    qstatus2->setFrameStyle(QFrame::Raised);
    qstatus2->setFrameShadow(QFrame::Sunken);
    qstatus2->setFrameShape(QFrame::Box);
    //qstatus2->setMinimumWidth(100);
    qstatus2->setMaximumWidth(100);
    qstatus2->setAlignment(Qt::AlignRight |Qt::AlignVCenter);
    qstatus=new QLabel("",this);
    qprogress->hide();
    ui->statusbar->setMinimumWidth(200);
    ui->statusbar->addPermanentWidget(qstatus,0);
    ui->statusbar->addPermanentWidget(qprogress,1);
    ui->statusbar->addPermanentWidget(qstatus2,0);

    if (!PROFILE.Exists("ShowDebug")) PROFILE["ShowDebug"]=false;
    ui->actionDebug->setChecked(PROFILE["ShowDebug"].toBool());

    if (!PROFILE["ShowDebug"].toBool()) {
        ui->logText->hide();
    }

    // TODO: Move all this to profile creation.

    // This speeds up the second part of importing craploads.. later it will speed up the first part too.
    if (!PROFILE.Exists("EnableMultithreading")) PROFILE["EnableMultithreading"]=QThread::idealThreadCount()>1;

    if (!PROFILE.Exists("MemoryHog")) PROFILE["MemoryHog"]=false;
    if (!PROFILE.Exists("EnableGraphSnapshots")) PROFILE["EnableGraphSnapshots"]=true;
    if (!PROFILE.Exists("SquareWavePlots")) PROFILE["SquareWavePlots"]=false;
    if (!PROFILE.Exists("EnableOximetry")) PROFILE["EnableOximetry"]=false;
    if (!PROFILE.Exists("LinkGroups")) PROFILE["LinkGroups"]=false;
    if (!PROFILE.Exists("AlwaysShowOverlayBars")) PROFILE["AlwaysShowOverlayBars"]=0;
    if (!PROFILE.Exists("UseAntiAliasing")) PROFILE["UseAntiAliasing"]=false;
    if (!PROFILE.Exists("IntentionalLeak")) PROFILE["IntentionalLeak"]=(double)0.0;
    if (!PROFILE.Exists("IgnoreShorterSessions")) PROFILE["IgnoreShorterSessions"]=0;
    if (!PROFILE.Exists("CombineCloserSessions")) PROFILE["CombineCloserSessions"]=0;
    if (!PROFILE.Exists("DaySplitTime")) PROFILE["DaySplitTime"]=QTime(12,0,0,0);
    if (!PROFILE.Exists("EventWindowSize")) PROFILE["EventWindowSize"]=4;
    if (!PROFILE.Exists("SPO2DropPercentage")) PROFILE["PulseChangeDuration"]=4;
    if (!PROFILE.Exists("SPO2DropDuration")) PROFILE["PulseChangeDuration"]=5;
    if (!PROFILE.Exists("PulseChangeBPM")) PROFILE["PulseChangeDuration"]=5;
    if (!PROFILE.Exists("PulseChangeDuration")) PROFILE["PulseChangeDuration"]=8;
    if (!PROFILE.Exists("GraphHeight")) PROFILE["GraphHeight"]=180;
    if (!PROFILE.Exists("OxiDiscardThreshold")) PROFILE["OxiDiscardThreshold"]=10;

    //ui->actionUse_AntiAliasing->setChecked(PROFILE["UseAntiAliasing"].toBool());
    ui->action_Link_Graph_Groups->setChecked(PROFILE["LinkGroups"].toBool());

    first_load=true;

    // Using the dirty registry here. :(
    QSettings settings("Jedimark", "SleepyHead");
    this->restoreGeometry(settings.value("MainWindow/geometry").toByteArray());

    ui->tabWidget->setCurrentWidget(ui->welcome);

    netmanager = new QNetworkAccessManager(this);
    connect(netmanager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));

    connect(ui->webView, SIGNAL(statusBarMessage(QString)), this, SLOT(updatestatusBarMessage(QString)));

    if (QSystemTrayIcon::isSystemTrayAvailable() && QSystemTrayIcon::supportsMessages()) {
        systray=new QSystemTrayIcon(QIcon(":/docs/sheep.png"),this);
        systray->show();
        systraymenu=new QMenu(this);
        systray->setContextMenu(systraymenu);
        QAction *a=systraymenu->addAction("SleepyHead v"+PREF["VersionString"].toString());
        a->setEnabled(false);
        systraymenu->addSeparator();
        systraymenu->addAction("About",this,SLOT(on_action_About_triggered()));
        systraymenu->addAction("Check for Updates",this,SLOT(on_actionCheck_for_Updates_triggered()));
        systraymenu->addSeparator();
        systraymenu->addAction("Exit",this,SLOT(close()));
    } else {
        systray=NULL;
        systraymenu=NULL;
    }

}
extern MainWindow *mainwin;
MainWindow::~MainWindow()
{
    if (systray) delete systray;
    if (systraymenu) delete systraymenu;

    //if (!isMaximized()) {
        QSettings settings("Jedimark", "SleepyHead");
        settings.setValue("MainWindow/geometry", saveGeometry());
    //}
    if (daily) {
        daily->close();
        delete daily;
    }
    if (overview) {
        overview->close();
        delete overview;
    }
    if (oximetry) {
        oximetry->close();
        delete oximetry;
    }
    DoneGraphs();
    Profiles::Done();
    mainwin=NULL;
    delete ui;
}
void MainWindow::Notify(QString s,int ms)
{
    if (systray) {
        systray->showMessage("SleepyHead v"+PREF["VersionString"].toString(),s,QSystemTrayIcon::Information,ms);
    } else {
        ui->statusbar->showMessage(s,ms);
    }
}

void MainWindow::Startup()
{
    qDebug() << PREF["AppName"].toString().toAscii()+" v"+PREF["VersionString"].toString().toAscii() << "built with Qt"<< QT_VERSION_STR << "on" << __DATE__ << __TIME__;
    qstatus->setText(tr("Loading Data"));
    qprogress->show();
    //qstatusbar->showMessage(tr("Loading Data"),0);

    // profile is a global variable set in main after login
    PROFILE.LoadMachineData();

    daily=new Daily(ui->tabWidget,NULL,this);
    ui->tabWidget->insertTab(1,daily,tr("Daily"));

    overview=new Overview(ui->tabWidget,daily->graphView());
    ui->tabWidget->insertTab(2,overview,tr("Overview"));
    if (PROFILE["EnableOximetry"].toBool()) {
        oximetry=new Oximetry(ui->tabWidget,daily->graphView());
        ui->tabWidget->insertTab(3,oximetry,tr("Oximetry"));
    }

    SnapshotGraph=new gGraphView(this,daily->graphView());
    SnapshotGraph->setMaximumSize(1024,512);
    SnapshotGraph->setMinimumSize(1024,512);
    //ReportGraph->setMaximumSize(graph_print_width,graph_print_height);
    //ReportGraph->setMinimumSize(graph_print_width,graph_print_height);
    SnapshotGraph->hide();

    if (daily) daily->ReloadGraphs();
    if (overview) overview->ReloadGraphs();
    qprogress->hide();
    qstatus->setText("");
}

void MainWindow::on_action_Import_Data_triggered()
{
    QStringList importLocations;
    {
        QString filename=PROFILE.Get("{DataFolder}/ImportLocations.txt");
        QFile file(filename);
        file.open(QFile::ReadOnly);
        QTextStream textStream(&file);
        while (1) {
            QString line = textStream.readLine();
             if (line.isNull())
                 break;
             else if (line.isEmpty())
                 continue;
             else {
                 importLocations.append(line);
             }
        }
        file.close();
    }

    bool addnew=false;
    QString newdir;

    bool asknew=false;
    if (importLocations.size()==0) {
        asknew=true;
    } else {
        int res=QMessageBox::question(this,"Import from where?","Do you just want to Import from the usual (remembered) locations?\n","The Usual","New Location","Cancel",0,2);
        if (res==1) {
            asknew=true;
        }
        if (res==2) return;
    }

    QStringList importFrom=importLocations;

    if (asknew) {
        QFileDialog w;
        w.setFileMode(QFileDialog::DirectoryOnly);
        w.setOption(QFileDialog::DontUseNativeDialog,true);

        QListView *l = w.findChild<QListView*>("listView");
        if (l) {
            l->setSelectionMode(QAbstractItemView::MultiSelection);
        }
        QTreeView *t = w.findChild<QTreeView*>();
        if (t) {
            t->setSelectionMode(QAbstractItemView::MultiSelection);
        }
        if (w.exec()!=QDialog::Accepted) {
            return;
        }
        for (int i=0;i<w.selectedFiles().size();i++) {
            QString newdir=w.selectedFiles().at(i);
            if (!importFrom.contains(newdir)) {
                importFrom.append(newdir);
                addnew=true;
            }
        }
    }

    int successful=false;

    QStringList goodlocations;
    for (int i=0;i<importFrom.size();i++) {
        QString dir=importFrom[i];
        if (!dir.isEmpty()) {
            qprogress->setValue(0);
            qprogress->show();
            qstatus->setText(tr("Importing Data"));
            int c=PROFILE.Import(dir);
            qDebug() << "Finished Importing data" << c;
            if (c) {
                if (!importLocations.contains(dir))
                    goodlocations.push_back(dir);
                successful=true;
            }
            qstatus->setText("");
            qprogress->hide();
        }
    }
    if (successful) {
        PROFILE.Save();
        if (daily) daily->ReloadGraphs();
        if (overview) overview->ReloadGraphs();
        if ((goodlocations.size()>0) && (QMessageBox::question(this,"Remember this Location?","Would you like to remember this import location for next time?\n"+newdir,QMessageBox::Yes,QMessageBox::No)==QMessageBox::Yes)) {
            for (int i=0;i<goodlocations.size();i++) {
                importLocations.push_back(goodlocations[i]);
            }
            QString filename=PROFILE.Get("{DataFolder}/ImportLocations.txt");
            QFile file(filename);
            file.open(QFile::WriteOnly);
            QTextStream ts(&file);
            for (int i=0;i<importLocations.size();i++) {
                ts << importLocations[i] << endl;
               //file.write(importLocations[i].toUtf8());
            }
            file.close();
        }
    } else {
        mainwin->Notify("Import Problem\n\nCouldn't find any new Machine Data at the locations given");
    }
}
QMenu * MainWindow::CreateMenu(QString title)
{
    QMenu *menu=new QMenu(title,ui->menubar);
    ui->menubar->insertMenu(ui->menu_Help->menuAction(),menu);
    return menu;
}

void MainWindow::on_actionView_Welcome_triggered()
{
    ui->tabWidget->setCurrentWidget(ui->welcome);
}

void MainWindow::on_action_Fullscreen_triggered()
{
    if (ui->action_Fullscreen->isChecked())
        this->showFullScreen();
    else
        this->showNormal();
}

void MainWindow::on_homeButton_clicked()
{
    QString file="qrc:/docs/index.html";
    QUrl url(file);
    ui->webView->setUrl(url);
}

void MainWindow::on_backButton_clicked()
{
    ui->webView->back();
}

void MainWindow::on_forwardButton_clicked()
{
    ui->webView->forward();
}

void MainWindow::on_webView_urlChanged(const QUrl &arg1)
{
    ui->urlBar->setEditText(arg1.toString());
}

void MainWindow::on_urlBar_activated(const QString &arg1)
{
    QUrl url(arg1);
    ui->webView->setUrl(url);
}

void MainWindow::on_dailyButton_clicked()
{
    ui->tabWidget->setCurrentWidget(daily);
    daily->RedrawGraphs();
    qstatus2->setText("Daily");
}
void MainWindow::JumpDaily()
{
    on_dailyButton_clicked();
}

void MainWindow::on_overviewButton_clicked()
{
    ui->tabWidget->setCurrentWidget(overview);
    qstatus2->setText("Overview");
}

void MainWindow::on_webView_loadFinished(bool arg1)
{
    arg1=arg1;
    qprogress->hide();
    if (first_load) {
        QTimer::singleShot(0,this,SLOT(Startup()));
        first_load=false;
    } else {
        qstatus->setText("");
    }
    ui->backButton->setEnabled(ui->webView->history()->canGoBack());
    ui->forwardButton->setEnabled(ui->webView->history()->canGoForward());
}

void MainWindow::on_webView_loadStarted()
{
    if (!first_load) {
        qstatus->setText(tr("Loading"));
        qprogress->reset();
        qprogress->show();
    }
}

void MainWindow::on_webView_loadProgress(int progress)
{
    qprogress->setValue(progress);
}

void MainWindow::on_action_About_triggered()
{

    QString gitrev=QString(GIT_REVISION);
    if (!gitrev.isEmpty()) gitrev="Revision: "+gitrev;

     QString msg=tr("<html><body><div align='center'><h2>SleepyHead v%1.%2.%3</h2>Build Date: %4 %5<br/>%6<hr>"
"Copyright &copy;2011 Mark Watkins (jedimark) <br> \n"
"<a href='http://sleepyhead.sourceforge.net'>http://sleepyhead.sourceforge.net</a> <hr>"
"This software is released under the GNU Public License <br>"
"<i>This software comes with absolutely no warranty, either express of implied. It comes with no guarantee of fitness for any particular purpose. No guarantees are made regarding the accuracy of any data this program displays."
                    "</div></body></html>").arg(major_version).arg(minor_version).arg(revision_number).arg(__DATE__).arg(__TIME__).arg(gitrev);
    QMessageBox msgbox(QMessageBox::Information,tr("About SleepyHead"),"",QMessageBox::Ok,this);
    msgbox.setTextFormat(Qt::RichText);
    msgbox.setText(msg);
    msgbox.exec();
}

void MainWindow::on_actionDebug_toggled(bool checked)
{
    PROFILE["ShowDebug"]=checked;
    if (checked) {
        ui->logText->show();
    } else {
        ui->logText->hide();
    }
}

void MainWindow::on_action_Reset_Graph_Layout_triggered()
{
    if (daily && (ui->tabWidget->currentWidget()==daily)) daily->ResetGraphLayout();
    if (overview && (ui->tabWidget->currentWidget()==overview)) overview->ResetGraphLayout();
}

void MainWindow::on_action_Preferences_triggered()
{
    PreferencesDialog pd(this,p_profile);
    prefdialog=&pd;
    if (pd.exec()==PreferencesDialog::Accepted) {
        qDebug() << "Preferences Accepted";
        pd.Save();
        if (daily) {
            //daily->ReloadGraphs();
            daily->RedrawGraphs();
        }
        if (overview) {
            overview->ReloadGraphs();
            overview->RedrawGraphs();
        }
    }
    prefdialog=NULL;
}
void MainWindow::selectOximetryTab()
{
    on_oximetryButton_clicked();
}

void MainWindow::on_oximetryButton_clicked()
{
    bool first=false;
    if (!oximetry) {
        if (!PROFILE.Exists("EnableOximetry") || !PROFILE["EnableOximetry"].toBool()) {
            if (QMessageBox::question(this,"Question","Do you have a CMS50[x] Oximeter?\nOne is required to use this section.",QMessageBox::Yes,QMessageBox::No)==QMessageBox::No) return;
            PROFILE["EnableOximetry"]=true;
        }
        oximetry=new Oximetry(ui->tabWidget,daily->graphView());
        ui->tabWidget->insertTab(3,oximetry,tr("Oximetry"));
        first=true;
    }
    ui->tabWidget->setCurrentWidget(oximetry);
    if (!first) oximetry->RedrawGraphs();
    qstatus2->setText("Oximetry");
}

void MainWindow::CheckForUpdates()
{
    mainwin->Notify("Checking for Updates");
    on_actionCheck_for_Updates_triggered();
}

void MainWindow::on_actionCheck_for_Updates_triggered()
{
    if (PREF.Exists("Updates_LastChecked")) {
        if (PREF["Updates_LastChecked"].toDateTime().secsTo(QDateTime::currentDateTime())<7200) {
            // Instead of doing this, just use the cached crud
            if (prefdialog) prefdialog->RefreshLastChecked();
            mainwin->Notify("No New Updates - You already checked recently...");
            return;
        }
    }
    netmanager->get(QNetworkRequest(QUrl("http://sleepyhead.sourceforge.net/current_version.txt")));
}
void MainWindow::replyFinished(QNetworkReply * reply)
{
    if (reply->error()==QNetworkReply::NoError) {
        // Wrap this crap in XML/JSON so can do other stuff.
        if (reply->size()>20) {
            qDebug() << "Doesn't look like a version file... :(";
        } else {
            // check in size
            QByteArray data=reply->readAll();
            QString a=data;
            a=a.trimmed();
            PREF["Updates_LastChecked"]=QDateTime::currentDateTime();
            if (prefdialog) prefdialog->RefreshLastChecked();
            if (a>PREF["VersionString"].toString()) {
                if (QMessageBox::question(this,"New Version","A newer version of SleepyHead is available, v"+a+".\nWould you like to update?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::Yes) {
                    QString fileurl="http://sourceforge.net/projects/sleepyhead/files/";
                    QString msg="<html><p>Sorry, I haven't implemented the auto-update yet</p>";
                    msg+="<a href='"+fileurl+"'>Click Here</a> for a link to the latest version";
                    msg+="</html>";

                    QMessageBox::information(this,"Laziness Warning",msg,QMessageBox::Ok);
                }
            } else {
                mainwin->Notify("Checked for Updates: SleepyHead is already up to date!");
            }
       }
    } else {
        qDebug() << "Network Error:" << reply->errorString();
    }
    reply->deleteLater();
}

void MainWindow::on_action_Screenshot_triggered()
{

    QTimer::singleShot(250,this,SLOT(DelayedScreenshot()));
}
void MainWindow::DelayedScreenshot()
{
    QPixmap pixmap = QPixmap::grabWindow(this->winId());
    QString a=PREF.Get("{home}")+"/Screenshots";
    QDir dir(a);
    if (!dir.exists()){
        dir.mkdir(a);
    }
    a+="/screenshot-"+QDateTime::currentDateTime().toString(Qt::ISODate)+".png";
    pixmap.save(a);
}

void MainWindow::on_actionView_O_ximetry_triggered()
{
    on_oximetryButton_clicked();
}
void MainWindow::updatestatusBarMessage (const QString & text)
{
    ui->statusbar->showMessage(text,1000);
}

void MainWindow::on_actionPrint_Report_triggered()
{
    if (ui->tabWidget->currentWidget()==overview) {
        PrintReport(overview->graphView(),"Overview");
    } else if (ui->tabWidget->currentWidget()==daily) {
        PrintReport(daily->graphView(),"Daily",daily->getDate());
    } else if (ui->tabWidget->currentWidget()==oximetry) {
        if (oximetry)
            PrintReport(oximetry->graphView(),"Oximetry");
    } else {
        //QPrinter printer();
        //ui->webView->print(printer)
        QMessageBox::information(this,"Not supported Yet","Sorry, printing from this page is not supported yet",QMessageBox::Ok);
    }
}

void MainWindow::on_action_Edit_Profile_triggered()
{
    NewProfile newprof(this);
    newprof.edit(PREF["Profile"].toString());
    newprof.exec();

}

void MainWindow::on_action_Link_Graph_Groups_toggled(bool arg1)
{
    PROFILE["LinkGroups"]=arg1;
    if (daily) daily->RedrawGraphs();
}

void MainWindow::on_action_CycleTabs_triggered()
{
    int i;
    qDebug() << "Switching Tabs";
    i=ui->tabWidget->currentIndex()+1;
    if (i >= ui->tabWidget->count())
        i=0;
    ui->tabWidget->setCurrentIndex(i);
}

void MainWindow::on_actionExp_ort_triggered()
{
    ExportCSV ex(this);
    if (ex.exec()==ExportCSV::Accepted) {
    }
}

void MainWindow::on_actionOnline_Users_Guide_triggered()
{
    ui->webView->load(QUrl("http://sourceforge.net/apps/mediawiki/sleepyhead/index.php?title=SleepyHead_Users_Guide"));
    ui->tabWidget->setCurrentIndex(0);
}

void MainWindow::on_action_Frequently_Asked_Questions_triggered()
{
    ui->webView->load(QUrl("http://sourceforge.net/apps/mediawiki/sleepyhead/index.php?title=Frequently_Asked_Questions"));
    ui->tabWidget->setCurrentIndex(0);
}

EventList *packEventList(EventList *ev)
{
    EventList *nev=new EventList(EVL_Event);

    EventDataType val,lastval=0;
    qint64 time,lasttime=0,lasttime2=0;

    lastval=ev->data(0);
    lasttime=ev->time(0);
    nev->AddEvent(lasttime,lastval);

    for (unsigned i=1;i<ev->count();i++) {
        val=ev->data(i);
        time=ev->time(i);
        if (val!=lastval) {
            nev->AddEvent(time,val);
            lasttime2=time;
        }
        lastval=val;
        lasttime=time;
    }
    if (val==lastval) {
        nev->AddEvent(lasttime,val);
    }
    return nev;
}

void MainWindow::PrintReport(gGraphView *gv,QString name, QDate date)
{
    if (!gv) return;
    //QDate d=QDate::currentDate();

    int visgraphs=gv->visibleGraphs();
    if (visgraphs==0) {
        Notify("There are no graphs visible to print");
        return;
    }

    QString username=PROFILE.Get("_{Username}_");

    //QPrinter printer(QPrinter::ScreenResolution);
    QPrinter printer(QPrinter::HighResolution);
#ifdef Q_WS_X11
    printer.setPrinterName("Print to File (PDF)");
    printer.setOutputFormat(QPrinter::PdfFormat);
    QString filename=PREF.Get("{home}/"+name+username+date.toString(Qt::ISODate)+".pdf");//QFileDialog::getSaveFileName(this,"Select filename to save PDF report to",,"PDF Files (*.pdf)");

    printer.setOutputFileName(filename);
#endif
    printer.setPrintRange(QPrinter::AllPages);
    printer.setOrientation(QPrinter::Portrait);
    printer.setFullPage(false); // This has nothing to do with scaling
    printer.setNumCopies(1);
    printer.setPageMargins(10,10,10,10,QPrinter::Millimeter);
    QPrintDialog *dialog = new QPrintDialog(&printer);
    if ( dialog->exec() != QDialog::Accepted) {
        return;
    }

    Notify("Printing "+name+" Report");
    QPainter painter;
    painter.begin(&printer);

    QRect res=printer.pageRect();
    qDebug() << "Printer Resolution is" << res.width() << "x" << res.height();

    const int graphs_per_page=5;
    const int footer_height=(res.height()/20);
    float gw=res.width();
    float gh=(res.height()-footer_height)/graphs_per_page;
    float gw2=gv->width();
    //float gh2=gv->totalHeight();
    float xscale=gw / gw2;
    float yscale=gh / PROFILE["GraphHeight"].toDouble();
    float div=1;
    if (gh>500)
        div=4;
    //yscale=xscale;
    SnapshotGraph->setPrintScaleX(xscale/div);
    SnapshotGraph->setPrintScaleY(yscale/div);

    mainwin->snapshotGraph()->setMinimumSize(gw/div,gh/div);
    mainwin->snapshotGraph()->setMaximumSize(gw/div,gh/div);

    int page=1;
    int pages=ceil(float(visgraphs+1)/float(graphs_per_page));
    int i=0;
    int top=0;
    int gcnt=0;
    if (qprogress) {
        qprogress->setValue(0);
        qprogress->setMaximum(gv->size());
        qprogress->show();
    }

    //int header_height=200;
    QString title=name+" Report";
    QTextOption t_op(Qt::AlignCenter);
    painter.setFont(*bigfont);
    QRectF bounds=painter.boundingRect(QRectF(0,0,res.width(),0),title,QTextOption(Qt::AlignCenter));
    painter.drawText(bounds,title,QTextOption(Qt::AlignCenter));
    painter.setFont(*defaultfont);
    top=bounds.height();
    //top+=15*yscale; //spacer
    int maxy=0;
    if (!PROFILE["FirstName"].toString().isEmpty()) {
        QString userinfo="Name:\t"+PROFILE["LastName"].toString()+", "+PROFILE["FirstName"].toString()+"\n";
        userinfo+="DOB:\t"+PROFILE["DOB"].toString()+"\n";
        userinfo+="Phone:\t"+PROFILE["Phone"].toString()+"\n";
        userinfo+="Email:\t"+PROFILE["EmailAddress"].toString()+"\n";
        if (!PROFILE["Address"].toString().isEmpty()) userinfo+="\nAddress:\n"+PROFILE["Address"].toString()+"\n";
        QRectF bounds=painter.boundingRect(QRectF(0,top,res.width(),0),userinfo,QTextOption(Qt::AlignLeft));
        painter.drawText(bounds,userinfo,QTextOption(Qt::AlignLeft));
        if (bounds.height()>maxy) maxy=bounds.height();
    }
    if (name=="Daily") {
        QString cpapinfo="Date: "+date.toString(Qt::SystemLocaleLongDate)+"\n";
        Day *cpap=PROFILE.GetDay(date,MT_CPAP);
        if (cpap) {
            time_t f=cpap->first()/1000L;
            time_t l=cpap->last()/1000L;
            int tt=qint64(cpap->total_time())/1000L;
            int h=tt/3600;
            int m=(tt/60)%60;
            int s=tt % 60;

            cpapinfo+="Mask Time: "+QString().sprintf("%2i hours %2i minutes, %2i seconds",h,m,s)+"\n";
            cpapinfo+="Bedtime: "+QDateTime::fromTime_t(f).time().toString("HH:mm:ss")+"\n";
            cpapinfo+="Wake-up: "+QDateTime::fromTime_t(l).time().toString("HH:mm:ss")+"\n\n";
            float ahi=(cpap->count(CPAP_Obstructive)+cpap->count(CPAP_Hypopnea)+cpap->count(CPAP_ClearAirway)+cpap->count(CPAP_Apnea))/cpap->hours();
            float csr=(100.0/cpap->hours())*(cpap->sum(CPAP_CSR)/3600.0);
            float uai=cpap->count(CPAP_Apnea)/cpap->hours();
            float oai=cpap->count(CPAP_Obstructive)/cpap->hours();
            float hi=(cpap->count(CPAP_ExP)+cpap->count(CPAP_Hypopnea))/cpap->hours();
            float cai=cpap->count(CPAP_ClearAirway)/cpap->hours();
            float rei=cpap->count(CPAP_RERA)/cpap->hours();
            float vsi=cpap->count(CPAP_VSnore)/cpap->hours();
            float fli=cpap->count(CPAP_FlowLimit)/cpap->hours();
            float nri=cpap->count(CPAP_NRI)/cpap->hours();
            float lki=cpap->count(CPAP_LeakFlag)/cpap->hours();
            float exp=cpap->count(CPAP_ExP)/cpap->hours();

            QString stats;
            stats="AHI\t"+QString::number(ahi,'f',2)+"\n";
            stats+="AI \t"+QString::number(oai,'f',2)+"\n";
            stats+="HI \t"+QString::number(hi,'f',2)+"\n";
            stats+="CAI\t"+QString::number(cai,'f',2)+"\n";
            if (cpap->machine->GetClass()=="PRS1") {
                stats+="REI\t"+QString::number(rei,'f',2)+"\n";
                stats+="VSI\t"+QString::number(vsi,'f',2)+"\n";
                stats+="FLI\t"+QString::number(fli,'f',2)+"\n";
                stats+="PB/CSR\t"+QString::number(csr,'f',2)+"%\n";
            } else if (cpap->machine->GetClass()=="ResMed") {
                stats+="UAI\t"+QString::number(uai,'f',2)+"\n";
            } else if (cpap->machine->GetClass()=="Intellipap") {
                stats+="NRI\t"+QString::number(nri,'f',2)+"\n";
                stats+="LKI\t"+QString::number(lki,'f',2)+"\n";
                stats+="EPI\t"+QString::number(exp,'f',2)+"\n";
            }
            QRectF bounds=painter.boundingRect(QRectF(res.width()-(250*xscale),top,250*xscale,0),stats,QTextOption(Qt::AlignRight));
            painter.drawText(bounds,stats,QTextOption(Qt::AlignRight));
            if (bounds.height()>maxy) maxy=bounds.height();
        }
        QRectF bounds=painter.boundingRect(QRectF((res.width()/2)-(res.width()/6),top,res.width()-(250*xscale),0),cpapinfo,QTextOption(Qt::AlignLeft));
        painter.drawText(bounds,cpapinfo,QTextOption(Qt::AlignLeft));
        if (bounds.height()>maxy) maxy=bounds.height();
    } else if (name=="Overview") {
        QDateTime first=QDateTime::fromTime_t((*gv)[0]->min_x/1000L);
        QDateTime last=QDateTime::fromTime_t((*gv)[0]->max_x/1000L);
        QString ovinfo="Reporting from "+first.date().toString(Qt::SystemLocaleShortDate)+" to "+last.date().toString(Qt::SystemLocaleShortDate);
        QRectF bounds=painter.boundingRect(QRectF(250*xscale,top,res.width()-(250*xscale),0),ovinfo,QTextOption(Qt::AlignLeft));
        painter.drawText(bounds,ovinfo,QTextOption(Qt::AlignLeft));

        if (bounds.height()>maxy) maxy=bounds.height();
    }
    top+=maxy;
   // top+=15*yscale; //spacer
    //top=header_height;

    bool first=true;
    do {
        //+" on "+d.toString(Qt::SystemLocaleLongDate)
        if (first) {
            QString footer="SleepyHead v"+PREF["VersionString"].toString()+" - http://sleepyhead.sourceforge.net";

            QRectF bounds=painter.boundingRect(QRectF(0,res.height()-footer_height,res.width(),footer_height),footer,QTextOption(Qt::AlignHCenter));
            painter.drawText(bounds,footer,QTextOption(Qt::AlignHCenter));

            QRectF pagebnds(res.width()-80*xscale,res.height()-footer_height,80*xscale,footer_height);
            painter.drawText(pagebnds,"Page "+QString::number(page)+" of "+QString::number(pages),QTextOption(Qt::AlignRight));
            first=false;
        }
        gGraph *g=(*gv)[i];
        if (g->isEmpty()) continue;
        if (!g->visible()) continue;
        g->deselect();
        QPixmap pm=g->renderPixmap(gw/div,gh/div);
        QPixmap pm2=pm.scaledToWidth(res.width());
        painter.drawPixmap(0,top,pm2.width(),pm2.height(),pm2);
        top+=gh;
        gcnt++;
        if ((gcnt>=graphs_per_page) || (top+gh>(res.height()-footer_height))) { //top+pm.height()>res.height()) {
            top=0;
            gcnt=0;
            //header_height=0;
            page++;
            if (page>pages) break;
            first=true;
            if (!printer.newPage()) {
                qWarning("failed in flushing page to disk, disk full?");
                break;
            }
        }
        if (qprogress) {
            qprogress->setValue(i);
            QApplication::processEvents();
        }
    } while (++i<gv->size());

    qprogress->hide();
    painter.end();
}

void MainWindow::on_action_Rebuild_Oximetry_Index_triggered()
{
    QVector<QString> valid;
    valid.push_back(OXI_Pulse);
    valid.push_back(OXI_SPO2);
    valid.push_back(OXI_Plethy);
    //valid.push_back(OXI_PulseChange); // Delete these and recalculate..
    //valid.push_back(OXI_SPO2Drop);

    QVector<QString> invalid;

    QVector<Machine *> machines=PROFILE.GetMachines(MT_OXIMETER);

    bool ok;
    int discard_threshold=PROFILE["OxiDiscardThreshold"].toInt(&ok);
    if (!ok) discard_threshold=10;
    Machine *m;
    for (int z=0;z<machines.size();z++) {
        m=machines[z];
        //m->sessionlist.erase(m->sessionlist.find(0));
        for (QHash<SessionID,Session *>::iterator s=m->sessionlist.begin();s!=m->sessionlist.end();s++) {
            Session *sess=s.value();
            if (!sess) continue;
            sess->OpenEvents();
            invalid.clear();
            for (QHash<ChannelID,QVector<EventList *> >::iterator e=sess->eventlist.begin();e!=sess->eventlist.end();e++) {
                if (!valid.contains(e.key())) {
                    for (int i=0;i<e.value().size();i++)  {
                        delete e.value()[i];
                    }
                    e.value().clear();
                    invalid.push_back(e.key());
                } else {
                    QVector<EventList *> newlist;
                    for (int i=0;i<e.value().size();i++)  {
                        if (e.value()[i]->count() > (unsigned)discard_threshold) {
                            newlist.push_back(e.value()[i]);
                        } else {
                            delete e.value()[i];
                        }
                    }
                    for (int i=0;i<newlist.size();i++) {
                        EventList *nev=packEventList(newlist[i]);
                        if (nev->count()!=e.value()[i]->count() ) {
                            delete newlist[i];
                            newlist[i]=nev;
                        } else {
                            delete nev;
                        }
                    }
                   e.value()=newlist;
                }
            }
            for (int i=0;i<invalid.size();i++) {
                sess->eventlist.erase(sess->eventlist.find(invalid[i]));
            }
            sess->m_cnt.clear();
            sess->m_sum.clear();
            sess->m_min.clear();
            sess->m_max.clear();
            sess->m_cph.clear();
            sess->m_sph.clear();
            sess->m_avg.clear();
            sess->m_wavg.clear();
            sess->m_90p.clear();
            sess->m_firstchan.clear();
            sess->m_lastchan.clear();
            sess->SetChanged(true);
        }

    }
    for (int i=0;i<machines.size();i++) {
        Machine *m=machines[i];
        m->Save();
    }
    getDaily()->ReloadGraphs();
    getOverview()->ReloadGraphs();
}

void MainWindow::on_actionChange_User_triggered()
{
    PROFILE.Save();
    PREF.Save();
    QString apppath;
#ifdef Q_OS_MAC
        // In Mac OS the full path of aplication binary is:
        //    <base-path>/myApp.app/Contents/MacOS/myApp

        // prune the extra bits to just get the app bundle path
        apppath=QApplication::instance()->applicationDirPath().section("/",0,-3);

        QStringList args;
        args << "-n" << apppath; // -n option is important, as it opens a new process

        if (QProcess::startDetached("/usr/bin/open",args)) {
            QApplication::instance()->exit();
        } else QMessageBox::warning(this,"Gah!","If you can read this, the restart command didn't work. Your going to have to do it yourself manually.",QMessageBox::Ok);

#else
        apppath=QApplication::instance()->applicationFilePath();

        // If this doesn't work on windoze, try uncommenting this method
        // Technically should be the same thing..

        //if (QDesktopServices::openUrl(apppath)) {
        //    QApplication::instance()->exit();
        //} else
        if (QProcess::startDetached(apppath)) {
            QApplication::instance()->exit();
        } else QMessageBox::warning(this,"Gah!","If you can read this, the restart command didn't work. Your going to have to do it yourself manually.",QMessageBox::Ok);
#endif

}
