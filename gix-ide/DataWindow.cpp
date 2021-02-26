#include "DataWindow.h"
#include "DataEntry.h"
#include "UiUtils.h"
#include "PathUtils.h"
#include "ListingFileParser.h"
#include "Ide.h"
#include "IdeTaskManager.h"
#include "MetadataManager.h"
#include "MdiChild.h"
#include "MainWindow.h"

#include <QToolBar>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QHeaderView>
#include <QBoxLayout>
#include <QPushButton>
#include <QInputDialog>
#include <QMdiSubWindow>
#include <QDir>
#include <QStandardItemModel>
#include <QMenu>

#define DEFAULT_NODE_EXPANDED false

DataWindow::DataWindow(QWidget *parent, MainWindow *mw) : QMainWindow(parent)
{
	this->cur_file = nullptr;

	this->setWindowTitle("Data");
	this->setMinimumWidth(300);
	this->setWindowFlags(Qt::Widget);
	QToolBar* toolBar = new QToolBar(this);
	this->addToolBar(toolBar);
	this->mainWindow = mw;

	bRefresh = new QPushButton(this);
	bRefresh->setFixedSize(16, 16);
	const QIcon refreshIcon = QIcon(":/icons/bullet_refresh.png");
	bRefresh->setIcon(refreshIcon);
	toolBar->addWidget(bRefresh);

	this->dataWidget = new QTreeWidget(this);
	this->setCentralWidget(dataWidget);
	dataWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
	dataWidget->setColumnCount(2);
	dataWidget->setUniformRowHeights(true);
	dataWidget->setHeaderLabels(QStringList() << tr("Name") << tr("Format"));

	connect(dataWidget, &QTreeWidget::itemDoubleClicked, this, &DataWindow::dataItemDoubleClicked);

	dataWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(dataWidget, &QTreeWidget::customContextMenuRequested, this, &DataWindow::prepareMenu);
	connect(GixGlobals::getMetadataManager(), &MetadataManager::updatedModuleMetadata, this, [this](CobolModuleMetadata *cmm) {
			refreshContent(); 
	});

	connect(Ide::TaskManager(), &IdeTaskManager::fileActivated, this, [this](ProjectFile *pf) {
		refreshContent();
	});

	connect(dataWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) { 
		dataWidget->resizeColumnToContents(0); 
		QVariant v = item->data(0, Qt::UserRole);
		if (v.isValid()) {
			if (v.userType() != QMetaType::QString) {
				DataEntry* e = (DataEntry*)v.value<void*>();
				Ide::TaskManager()->setIdeElementInfo(cur_file->GetFileFullPath() + ":" + e->path, 1);
			}
			else
				Ide::TaskManager()->setIdeElementInfo(cur_file->GetFileFullPath() + v.toString(), 1);
		}
	});

	connect(dataWidget, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
		dataWidget->resizeColumnToContents(0); 
		QVariant v = item->data(0, Qt::UserRole);
		if (v.isValid()) {
			DataEntry* e = (DataEntry*)v.value<void*>();
			if (v.userType() != QMetaType::QString) {
				DataEntry* e = (DataEntry*)v.value<void*>();
				Ide::TaskManager()->setIdeElementInfo(cur_file->GetFileFullPath() + ":" + e->path, 0);
			}
			else
				Ide::TaskManager()->setIdeElementInfo(cur_file->GetFileFullPath() + ":" + v.toString(), 0);
		}
	});
}


DataWindow::~DataWindow()
{
}

bool DataWindow::hasContent()
{
	return cur_file != nullptr;
}

void DataWindow::refreshContent()
{
	dataWidget->clear();

	Ide::TaskManager()->logMessage(GIX_CONSOLE_LOG, QString("DataWindow is refreshing"), QLogger::LogLevel::Debug);
	if (mainWindow->activeMdiChild() != nullptr) {
		QString f = mainWindow->activeMdiChild()->currentFile();

		ProjectCollection* ppj = Ide::TaskManager()->getCurrentProjectCollection();
		if (ppj != nullptr) {
			ProjectFile* pf = ppj->locateProjectFileByPath(f);
			if (pf != nullptr) {
				QDockWidget *qdw = dynamic_cast<QDockWidget *>(this->parent());
				if (qdw && qdw->isVisible()) {
					this->setContent(pf);
				}
			}
		}
	}
	else
		Ide::TaskManager()->logMessage(GIX_CONSOLE_LOG, "no window", QLogger::LogLevel::Trace);
}

void DataWindow::setContent(ProjectFile *pf)
{
	QString lstPath, module_name, output_path;

	QString configuration = Ide::TaskManager()->getCurrentConfiguration();
	QString platform = Ide::TaskManager()->getCurrentPlatform();

	if (pf == nullptr || pf->PropertyGetValue("build_action") != "compile" || !pf->getOutputModuleAndFile(configuration, platform, module_name, output_path)) {
		cur_file = nullptr;
		refresh_data_items();
		return;
	}

	CobolModuleMetadata* cfm = GixGlobals::getMetadataManager()->getModuleMetadataBySource(pf->GetFileFullPath());
	if (cfm) {
		cur_file = pf;
	}
	else {
		cur_file = nullptr;
	}
	refresh_data_items();
}

void DataWindow::IdeStatusChanged(IdeStatus s)
{

}

void DataWindow::onDebuggerBreak()
{

}

void DataWindow::refresh_data_items()
{
	dataWidget->clear();

	if (!cur_file)
		return;

	CobolModuleMetadata *cur_data = GixGlobals::getMetadataManager()->getModuleMetadataBySource(cur_file->GetFileFullPath());
	if (cur_data == nullptr)
		return;

	QTreeWidgetItem *li = new QTreeWidgetItem(dataWidget, QStringList("Linkage section"));
	QTreeWidgetItem *wi = new QTreeWidgetItem(dataWidget, QStringList("Working storage"));
	QTreeWidgetItem *fi = new QTreeWidgetItem(dataWidget, QStringList("File section"));

	li->setData(0, Qt::UserRole, "LS");
	wi->setData(0, Qt::UserRole, "WS");
	fi->setData(0, Qt::UserRole, "DS");

	QList<DataEntry *> entries = cur_data->getLinkageDataEntries();
	for (int i = 0; i < entries.size(); i++) {
		DataEntry *e = entries.at(i);
		append_children(e, li);
	}

	entries = cur_data->getWorkingStorageDataEntries();
	for (int i = 0; i < entries.size(); i++) {
		DataEntry *e = entries.at(i);
		append_children(e, wi);
	}

	entries = cur_data->getFileDataEntries();
	for (int i = 0; i < entries.size(); i++) {
		DataEntry *e = entries.at(i);
		append_children(e, fi);
	}

	setNodeStatus("LS", li);
	setNodeStatus("WS", wi);
	setNodeStatus("DS", fi);

	dataWidget->resizeColumnToContents(0);
}

void DataWindow::append_children(DataEntry *e, QTreeWidgetItem *parent_item)
{
	QTreeWidgetItem *si = new QTreeWidgetItem(QStringList() << e->name << e->format);
	si->setData(0, Qt::UserRole, QVariant::fromValue<void *>(e));
	parent_item->addChild(si);

	QString node_path = e->path;
	setNodeStatus(node_path, si);

	for (int i = 0; i < e->children.size(); i++) {
		append_children(e->children.at(i), si);
	}
}

void DataWindow::setNodeStatus(QString node_path, QTreeWidgetItem* si)
{
	int st = Ide::TaskManager()->getIdeElementInfo(cur_file->GetFileFullPath() + ":" + node_path).toInt();
	if (st >= 0)
		si->setExpanded(st);
	else
		si->setExpanded(DEFAULT_NODE_EXPANDED);
}

void DataWindow::dataItemDoubleClicked(QTreeWidgetItem * item, int column)
{
	QVariant v = item->data(0, Qt::UserRole);
	if (v.isValid()) {
		DataEntry* e = (DataEntry*) v.value<void *>();
		Ide::TaskManager()->gotoDefinition(e);
	}
}

void DataWindow::prepareMenu(const QPoint & pos)
{
	QTreeWidget *tree = dataWidget;
	QTreeWidgetItem *item = tree->itemAt(pos);
	QVariant q = item->data(0, Qt::UserRole);
	DataEntry *e = (DataEntry *)(q.value<void*>());
	if (!e)
		return;

	QMenu *menu = new QMenu(this);

	QAction *goToDefinitionAct = new QAction(QIcon(":/icons/book_go.png"), tr("Go to definition"), this);
	goToDefinitionAct->setStatusTip(tr("Go to definition"));
	connect(goToDefinitionAct, &QAction::triggered, this, [this, e] { Ide::TaskManager()->gotoDefinition(e); });
	menu->addAction(goToDefinitionAct);

	if (Ide::TaskManager()->getStatus() == IdeStatus::DebuggingOnBreak) {
		menu->addSeparator();

		QAction *addToWatchAct = new QAction(QIcon(":/icons/comment_add.png"), tr("Add to watch window"), this);
		addToWatchAct->setStatusTip(tr("Add to watch window"));
		connect(addToWatchAct, &QAction::triggered, this, [e] { 
			Ide::TaskManager()->getDebugManager()->addWatchedVar(e->name); 
			Ide::TaskManager()->refreshWatchWindow();
		});
		menu->addAction(addToWatchAct);
	}

	if (menu != nullptr) {
		QPoint pt(pos);
		menu->exec(tree->mapToGlobal(pos));
	}
}

//
//void DataWindow::addButtonClicked()
//{
//	bool ok;
//	QString text = QInputDialog::getText(this, tr("Enter a variable/file name"),
//		tr("Variable/field name:"), QLineEdit::Normal,
//		"", &ok);
//
//	if (!ok)
//		return;
//
//	if (text.isEmpty()) {
//		UiUtils::ErrorDialog(tr("Invalid variable/field name"));
//		return;
//	}
//
//	Ide::TaskManager()->getDebugManager()->addWatchedVar(text);
//
//	refreshContent();
//}