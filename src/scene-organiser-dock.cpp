/*
 * obs-scene-organiser
 * Copyright (C) 2026 Awet
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "scene-organiser-dock.hpp"
#include "plugin-support.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QDropEvent>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QFont>
#include <QMainWindow>
#include <QListWidget>
#include <QApplication>
#include <QContextMenuEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QToolButton>
#include <algorithm>

void OrgTree::dropEvent(QDropEvent *e)
{
	QTreeWidgetItem *target = itemAt(e->position().toPoint());
	DropIndicatorPosition indicator = dropIndicatorPosition();
	QList<QTreeWidgetItem *> dragged = selectedItems();
	bool draggingFolder = std::any_of(dragged.begin(), dragged.end(),
		[](QTreeWidgetItem *i) { return i->type() == OrgFolder; });
	if (draggingFolder && target && target->type() == OrgFolder && indicator == OnItem) {
		e->ignore();
		return;
	}
	QTreeWidget::dropEvent(e);
	emit itemDropped();
}

void ColorBarDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
	const auto *tw = static_cast<const QTreeWidget *>(parent());
	QTreeWidgetItem *item = tw->itemFromIndex(idx);
	if (item && item->type() == OrgSeparator) {
		p->fillRect(opt.rect, opt.palette.window());
		QColor c = idx.data(RoleColor).value<QColor>();
		if (!c.isValid()) c = QColor(110, 110, 110);
		int y = opt.rect.top() + opt.rect.height() / 2;
		p->fillRect(6, y - 1, opt.rect.right() - 6, 2, c);
		return;
	}
	QStyledItemDelegate::paint(p, opt, idx);
	QColor color = idx.data(RoleColor).value<QColor>();
	if (color.isValid())
		p->fillRect(0, opt.rect.top(), 4, opt.rect.height(), color);
}

static QIcon makeFolderIcon(const QColor &color)
{
	const int s = 16;
	QPixmap pm(s, s);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	QColor c = color.isValid() ? color : QColor(190, 190, 190);
	p.setPen(Qt::NoPen);
	p.setBrush(c);
	p.drawRoundedRect(QRectF(1.5, 3.0, 6.0, 3.5), 1.0, 1.0);
	p.drawRoundedRect(QRectF(1.5, 5.0, 13.0, 9.5), 1.5, 1.5);
	return QIcon(pm);
}

SceneOrganiserDock::SceneOrganiserDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("SceneOrganiserDock");
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);
	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(obs_module_text("Organiser.Search.Placeholder"));
	m_search->setClearButtonEnabled(true);
	root->addWidget(m_search);
	m_tree = new OrgTree();
	m_tree->setHeaderHidden(true);
	m_tree->setDragDropMode(QAbstractItemView::InternalMove);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	m_tree->setItemDelegate(new ColorBarDelegate(m_tree));
	m_tree->setStyleSheet("QTreeWidget::item { padding-left: 4px; min-height: 22px; }");
	root->addWidget(m_tree, 1);
	auto *bottomBar = new QHBoxLayout();
	bottomBar->setSpacing(2);
	bottomBar->setContentsMargins(0, 0, 0, 0);
	auto makeBtn = [this](const QString &text, const QString &tip) {
		auto *b = new QToolButton(this);
		b->setText(text);
		b->setToolTip(tip);
		b->setFixedSize(26, 24);
		b->setAutoRaise(true);
		QFont f = b->font();
		f.setPointSize(11);
		f.setBold(true);
		b->setFont(f);
		return b;
	};
	auto *addBtn  = makeBtn("+", obs_module_text("Organiser.Tip.Add"));
	auto *delBtn  = makeBtn(QString::fromUtf8("\u2212"), obs_module_text("Organiser.Tip.Delete"));
	auto *upBtn   = makeBtn(QString::fromUtf8("\u25B2"), obs_module_text("Organiser.Tip.MoveUp"));
	auto *downBtn = makeBtn(QString::fromUtf8("\u25BC"), obs_module_text("Organiser.Tip.MoveDown"));
	bottomBar->addWidget(addBtn);
	bottomBar->addWidget(delBtn);
	bottomBar->addWidget(upBtn);
	bottomBar->addWidget(downBtn);
	bottomBar->addStretch(1);
	root->addLayout(bottomBar);
	connect(addBtn,  &QToolButton::clicked, this, &SceneOrganiserDock::addItem);
	connect(delBtn,  &QToolButton::clicked, this, &SceneOrganiserDock::deleteSelected);
	connect(upBtn,   &QToolButton::clicked, this, [this] { moveSelected(-1); });
	connect(downBtn, &QToolButton::clicked, this, [this] { moveSelected(+1); });
	auto *upSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), m_tree);
	upSc->setContext(Qt::WidgetWithChildrenShortcut);
	connect(upSc, &QShortcut::activated, this, [this] { moveSelected(-1); });
	auto *downSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down), m_tree);
	downSc->setContext(Qt::WidgetWithChildrenShortcut);
	connect(downSc, &QShortcut::activated, this, [this] { moveSelected(+1); });
	auto *delSc = new QShortcut(QKeySequence::Delete, m_tree);
	delSc->setContext(Qt::WidgetWithChildrenShortcut);
	connect(delSc, &QShortcut::activated, this, &SceneOrganiserDock::deleteSelected);
	connect(m_search, &QLineEdit::textChanged, this, &SceneOrganiserDock::onSearchChanged);
	connect(m_tree, &QTreeWidget::itemClicked, this, &SceneOrganiserDock::onItemClicked);
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &SceneOrganiserDock::onItemDoubleClicked);
	connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SceneOrganiserDock::onContextMenu);
	connect(m_tree, &QTreeWidget::itemChanged, this, &SceneOrganiserDock::onItemChanged);
	connect(m_tree, &OrgTree::itemDropped, this, &SceneOrganiserDock::onItemDropped);
	obs_frontend_add_event_callback(frontendEvent, this);
}

SceneOrganiserDock::~SceneOrganiserDock()
{
	obs_frontend_remove_event_callback(frontendEvent, this);
}

void SceneOrganiserDock::frontendEvent(obs_frontend_event event, void *data)
{
	auto *self = static_cast<SceneOrganiserDock *>(data);
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		QMetaObject::invokeMethod(self, "onFinishedLoading", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		QMetaObject::invokeMethod(self, "syncScenes", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		QMetaObject::invokeMethod(self, "highlightCurrentScene", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		QMetaObject::invokeMethod(self, "onCollectionChanged", Qt::QueuedConnection);
		break;
	default:
		break;
	}
}

void SceneOrganiserDock::onFinishedLoading() { load(); m_loaded = true; syncScenes(); }
void SceneOrganiserDock::onCollectionChanged() { save(); load(); }

void SceneOrganiserDock::syncScenes()
{
	if (!m_loaded) return;
	m_inhibit = true;
	struct obs_frontend_source_list list = {0};
	obs_frontend_get_scenes(&list);
	QSet<QString> obsNames;
	for (size_t i = 0; i < list.sources.num; i++)
		obsNames.insert(QString::fromUtf8(obs_source_get_name(list.sources.array[i])));
	obs_frontend_source_list_free(&list);
	QSet<QString> treeNames;
	collectObsNames(nullptr, treeNames);
	for (const QString &name : obsNames)
		if (!treeNames.contains(name))
			makeSceneItem(nullptr, name);
	removeOrphans(nullptr, obsNames);
	m_inhibit = false;
	highlightCurrentScene();
	save();
}

void SceneOrganiserDock::highlightCurrentScene()
{
	obs_source_t *src = obs_frontend_get_current_scene();
	QString name = src ? QString::fromUtf8(obs_source_get_name(src)) : "";
	if (src) obs_source_release(src);
	highlightItems(nullptr, name);
}

QTreeWidgetItem *SceneOrganiserDock::makeSceneItem(QTreeWidgetItem *parent, const QString &name)
{
	QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent, OrgScene) : new QTreeWidgetItem(m_tree, OrgScene);
	item->setText(0, name);
	item->setData(0, RoleObsName, name);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
	return item;
}

QTreeWidgetItem *SceneOrganiserDock::makeFolderItem(QTreeWidgetItem *parent, const QString &name)
{
	QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent, OrgFolder) : new QTreeWidgetItem(m_tree, OrgFolder);
	item->setText(0, name);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
	item->setExpanded(true);
	item->setIcon(0, makeFolderIcon(QColor()));
	return item;
}

QTreeWidgetItem *SceneOrganiserDock::makeTextFieldItem(QTreeWidgetItem *parent, const QString &text)
{
	QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent, OrgTextField) : new QTreeWidgetItem(m_tree, OrgTextField);
	item->setText(0, text);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
	QFont f = item->font(0); f.setItalic(true); item->setFont(0, f);
	return item;
}

QTreeWidgetItem *SceneOrganiserDock::makeSeparatorItem(QTreeWidgetItem *parent)
{
	QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent, OrgSeparator) : new QTreeWidgetItem(m_tree, OrgSeparator);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
	item->setSizeHint(0, QSize(0, 22));
	return item;
}

void SceneOrganiserDock::collectObsNames(QTreeWidgetItem *parent, QSet<QString> &out) const
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child = parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene) out.insert(child->data(0, RoleObsName).toString());
		else if (child->type() == OrgFolder) collectObsNames(child, out);
	}
}

void SceneOrganiserDock::removeOrphans(QTreeWidgetItem *parent, const QSet<QString> &valid)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = n - 1; i >= 0; i--) {
		QTreeWidgetItem *child = parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene) {
			if (!valid.contains(child->data(0, RoleObsName).toString())) delete child;
		} else if (child->type() == OrgFolder) removeOrphans(child, valid);
	}
}

void SceneOrganiserDock::highlightItems(QTreeWidgetItem *parent, const QString &current)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child = parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene) {
			QFont f = child->font(0);
			f.setBold(child->data(0, RoleObsName).toString() == current);
			child->setFont(0, f);
		} else if (child->type() == OrgFolder) highlightItems(child, current);
	}
}

void SceneOrganiserDock::applyColor(QTreeWidgetItem *item, const QColor &color)
{
	item->setData(0, RoleColor, color);
	QColor bg = color; bg.setAlpha(45);
	item->setBackground(0, bg);
	if (item->type() == OrgFolder) item->setIcon(0, makeFolderIcon(color));
}

void SceneOrganiserDock::filterItems(QTreeWidgetItem *parent, const QString &text)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child = parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene) {
			child->setHidden(!(text.isEmpty() || child->text(0).toLower().contains(text)));
		} else if (child->type() == OrgFolder) {
			filterItems(child, text);
			bool anyVisible = false;
			for (int j = 0; j < child->childCount(); j++) if (!child->child(j)->isHidden()) { anyVisible = true; break; }
			bool folderMatch = !text.isEmpty() && child->text(0).toLower().contains(text);
			child->setHidden(!anyVisible && !folderMatch);
			if (!text.isEmpty()) child->setExpanded(anyVisible || folderMatch);
		}
	}
}

void SceneOrganiserDock::onItemClicked(QTreeWidgetItem *item, int) { if (item && item->type() == OrgScene) switchToScene(item); }
void SceneOrganiserDock::onItemDoubleClicked(QTreeWidgetItem *item, int) { if (item && item->type() == OrgScene) switchToScene(item); }
void SceneOrganiserDock::onSearchChanged(const QString &text) { filterItems(nullptr, text.trimmed().toLower()); }
void SceneOrganiserDock::onItemDropped() { save(); }

void SceneOrganiserDock::onItemChanged(QTreeWidgetItem *item, int col)
{
	if (m_inhibit || col != 0) return;
	if (item->type() == OrgScene) {
		QString newName = item->text(0);
		QString oldName = item->data(0, RoleObsName).toString();
		if (newName != oldName) {
			obs_source_t *src = obs_get_source_by_name(oldName.toUtf8().constData());
			if (src) { obs_source_set_name(src, newName.toUtf8().constData()); obs_source_release(src); }
			item->setData(0, RoleObsName, newName);
		}
	}
	save();
}

void SceneOrganiserDock::onContextMenu(const QPoint &pos)
{
	QTreeWidgetItem *item = m_tree->itemAt(pos);
	QMenu menu(this);
	if (item && item->type() == OrgSeparator) {
		menu.addAction(obs_module_text("Organiser.Action.DuplicateSeparator"), this, [this, item] { duplicateSeparator(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"), this, [this, item] { pickColor(item); });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"), this, [this, item] { clearColor(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.DeleteSeparator"), this, [this, item] { deleteSeparator(item); });
	} else if (item && item->type() == OrgTextField) {
		menu.addAction(obs_module_text("Organiser.Action.RenameTextField"), this, [this, item] { renameItem(item); });
		menu.addAction(obs_module_text("Organiser.Action.DuplicateTextField"), this, [this, item] { duplicateTextField(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"), this, [this, item] { pickColor(item); });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"), this, [this, item] { clearColor(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.DeleteTextField"), this, [this, item] { deleteTextField(item); });
	} else if (!item) {
		menu.addAction(obs_module_text("Organiser.Action.AddFolder"), this, &SceneOrganiserDock::addFolder);
		menu.addAction(obs_module_text("Organiser.Action.AddSeparator"), this, &SceneOrganiserDock::addSeparator);
		menu.addAction(obs_module_text("Organiser.Action.AddTextField"), this, &SceneOrganiserDock::addTextField);
	} else if (item->type() == OrgFolder) {
		menu.addAction(obs_module_text("Organiser.Action.RenameFolder"), this, [this, item] { renameItem(item); });
		menu.addAction(obs_module_text("Organiser.Action.DeleteFolder"), this, [this, item] { deleteFolder(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"), this, [this, item] { pickColor(item); });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"), this, [this, item] { clearColor(item); });
	} else if (item->type() == OrgScene) {
		menu.addAction(obs_module_text("Organiser.Action.SwitchToScene"), this, [this, item] { switchToScene(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.RenameScene"), this, [this, item] { renameItem(item); });
		menu.addAction(obs_module_text("Organiser.Action.DuplicateScene"), this, [this, item] { duplicateScene(item); });
		menu.addAction(obs_module_text("Organiser.Action.RemoveScene"), this, [this, item] { removeScene(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.OpenFilters"), this, [this, item] { openFilters(item); });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"), this, [this, item] { pickColor(item); });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"), this, [this, item] { clearColor(item); });
	}
	if (!menu.actions().isEmpty()) menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void SceneOrganiserDock::addItem()
{
	QMenu menu(this);
	menu.addAction(obs_module_text("Organiser.Action.AddFolder"), this, &SceneOrganiserDock::addFolder);
	menu.addAction(obs_module_text("Organiser.Action.AddSeparator"), this, &SceneOrganiserDock::addSeparator);
	menu.addAction(obs_module_text("Organiser.Action.AddTextField"), this, &SceneOrganiserDock::addTextField);
	auto *btn = qobject_cast<QPushButton *>(sender());
	QPoint pos = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QCursor::pos();
	menu.exec(pos);
}

void SceneOrganiserDock::addFolder()
{
	bool ok;
	QString name = QInputDialog::getText(this, obs_module_text("Organiser.Dialog.AddFolder.Title"), obs_module_text("Organiser.Dialog.AddFolder.Text"), QLineEdit::Normal, "", &ok);
	if (!ok || name.trimmed().isEmpty()) return;
	makeFolderItem(nullptr, name.trimmed());
	save();
}

void SceneOrganiserDock::addSeparator() { makeSeparatorItem(nullptr); save(); }
void SceneOrganiserDock::deleteSeparator(QTreeWidgetItem *item) { if (item->type() != OrgSeparator) return; delete item; save(); }

static void insertSiblingAfter(QTreeWidget *tree, QTreeWidgetItem *src, QTreeWidgetItem *neu)
{
	QTreeWidgetItem *parent = src->parent();
	int idx = parent ? parent->indexOfChild(src) : tree->indexOfTopLevelItem(src);
	if (parent) { parent->removeChild(neu); parent->insertChild(idx + 1, neu); }
	else { int ni = tree->indexOfTopLevelItem(neu); if (ni >= 0) tree->takeTopLevelItem(ni); tree->insertTopLevelItem(idx + 1, neu); }
}

void SceneOrganiserDock::duplicateSeparator(QTreeWidgetItem *item)
{
	if (item->type() != OrgSeparator) return;
	QTreeWidgetItem *neu = makeSeparatorItem(item->parent());
	QColor c = item->data(0, RoleColor).value<QColor>();
	if (c.isValid()) neu->setData(0, RoleColor, c);
	insertSiblingAfter(m_tree, item, neu);
	m_tree->setCurrentItem(neu);
	save();
}

void SceneOrganiserDock::duplicateTextField(QTreeWidgetItem *item)
{
	if (item->type() != OrgTextField) return;
	QTreeWidgetItem *neu = makeTextFieldItem(item->parent(), item->text(0));
	QColor c = item->data(0, RoleColor).value<QColor>();
	if (c.isValid()) applyColor(neu, c);
	insertSiblingAfter(m_tree, item, neu);
	m_tree->setCurrentItem(neu);
	save();
}

void SceneOrganiserDock::deleteSelected()
{
	QTreeWidgetItem *item = m_tree->currentItem();
	if (!item) return;
	switch (item->type()) {
	case OrgFolder:    deleteFolder(item);    break;
	case OrgScene:     removeScene(item);     break;
	case OrgSeparator: deleteSeparator(item); break;
	case OrgTextField: deleteTextField(item); break;
	}
}

void SceneOrganiserDock::moveSelected(int direction)
{
	QTreeWidgetItem *item = m_tree->currentItem();
	if (!item || direction == 0) return;
	QTreeWidgetItem *parent = item->parent();
	int idx = parent ? parent->indexOfChild(item) : m_tree->indexOfTopLevelItem(item);
	int count = parent ? parent->childCount() : m_tree->topLevelItemCount();
	int newIdx = idx + direction;
	if (newIdx < 0 || newIdx >= count) return;
	m_inhibit = true;
	bool wasExpanded = item->isExpanded();
	if (parent) { QTreeWidgetItem *taken = parent->takeChild(idx); parent->insertChild(newIdx, taken); }
	else { QTreeWidgetItem *taken = m_tree->takeTopLevelItem(idx); m_tree->insertTopLevelItem(newIdx, taken); }
	item->setExpanded(wasExpanded);
	m_tree->setCurrentItem(item);
	m_inhibit = false;
	save();
}

void SceneOrganiserDock::addTextField()
{
	bool ok;
	QString text = QInputDialog::getText(this, obs_module_text("Organiser.Dialog.AddTextField.Title"), obs_module_text("Organiser.Dialog.AddTextField.Text"), QLineEdit::Normal, "", &ok);
	if (!ok || text.trimmed().isEmpty()) return;
	makeTextFieldItem(nullptr, text.trimmed());
	save();
}

void SceneOrganiserDock::deleteTextField(QTreeWidgetItem *item) { if (item->type() != OrgTextField) return; delete item; save(); }
void SceneOrganiserDock::renameItem(QTreeWidgetItem *item) { m_tree->editItem(item, 0); }

void SceneOrganiserDock::deleteFolder(QTreeWidgetItem *item)
{
	if (item->type() != OrgFolder) return;
	int idx = m_tree->indexOfTopLevelItem(item);
	while (item->childCount() > 0) { QTreeWidgetItem *child = item->takeChild(0); m_tree->insertTopLevelItem(idx++, child); }
	delete item;
	save();
}

void SceneOrganiserDock::pickColor(QTreeWidgetItem *item)
{
	QColor initial = item->data(0, RoleColor).value<QColor>();
	QColor color = QColorDialog::getColor(initial.isValid() ? initial : Qt::red, this, obs_module_text("Organiser.Action.SetColor"));
	if (color.isValid()) { applyColor(item, color); save(); }
}

void SceneOrganiserDock::clearColor(QTreeWidgetItem *item)
{
	item->setData(0, RoleColor, QColor());
	item->setBackground(0, QBrush());
	if (item->type() == OrgFolder) item->setIcon(0, makeFolderIcon(QColor()));
	save();
}

void SceneOrganiserDock::switchToScene(QTreeWidgetItem *item)
{
	if (item->type() != OrgScene) return;
	QString name = item->data(0, RoleObsName).toString();
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (!src) return;
	if (obs_frontend_preview_program_mode_active()) obs_frontend_set_current_preview_scene(src);
	else obs_frontend_set_current_scene(src);
	obs_source_release(src);
}

void SceneOrganiserDock::duplicateScene(QTreeWidgetItem *item)
{
	if (item->type() != OrgScene) return;
	QString name = item->data(0, RoleObsName).toString();
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (!src) return;
	obs_scene_t *scene = obs_scene_from_source(src);
	if (!scene) { obs_source_release(src); return; }
	QString newName = name + " (copy)";
	int idx = 2;
	while (true) {
		obs_source_t *ex = obs_get_source_by_name(newName.toUtf8().constData());
		if (!ex) break;
		obs_source_release(ex);
		newName = name + QString(" (copy %1)").arg(idx++);
	}
	obs_scene_t *dup = obs_scene_duplicate(scene, newName.toUtf8().constData(), OBS_SCENE_DUP_REFS);
	if (dup) obs_scene_release(dup);
	obs_source_release(src);
}

void SceneOrganiserDock::removeScene(QTreeWidgetItem *item)
{
	if (item->type() != OrgScene) return;
	QString name = item->data(0, RoleObsName).toString();
	if (QMessageBox::question(this, obs_module_text("Organiser.Action.RemoveScene"),
		QString(obs_module_text("Organiser.Dialog.Remove.Text")).arg(name)) != QMessageBox::Yes) return;
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (src) { obs_source_remove(src); obs_source_release(src); }
}

void SceneOrganiserDock::openFilters(QTreeWidgetItem *item)
{
	if (item->type() != OrgScene) return;
	QString name = item->data(0, RoleObsName).toString();
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (src) { obs_frontend_open_source_filters(src); obs_source_release(src); }
}

void SceneOrganiserDock::sortChildren(QTreeWidgetItem *parent, Qt::SortOrder order)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	QList<QTreeWidgetItem *> items;
	items.reserve(n);
	for (int i = 0; i < n; i++) items.append(parent ? parent->child(i) : m_tree->topLevelItem(i));
	std::sort(items.begin(), items.end(), [order](QTreeWidgetItem *a, QTreeWidgetItem *b) {
		return order == Qt::AscendingOrder ? a->text(0).toLower() < b->text(0).toLower() : a->text(0).toLower() > b->text(0).toLower();
	});
	m_inhibit = true;
	if (parent) { for (auto *it : items) parent->removeChild(it); for (auto *it : items) parent->addChild(it); }
	else { for (auto *it : items) m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(it)); m_tree->addTopLevelItems(items); }
	m_inhibit = false;
	save();
}

QString SceneOrganiserDock::configPath() const
{
	char *dirRaw = obs_module_config_path("scenes/");
	QString dir = QString::fromUtf8(dirRaw); bfree(dirRaw);
	QDir().mkpath(dir);
	char *colRaw = obs_frontend_get_current_scene_collection();
	QString col = QString::fromUtf8(colRaw).replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "_");
	bfree(colRaw);
	return dir + col + ".json";
}

void SceneOrganiserDock::save()
{
	if (m_inhibit || !m_loaded) return;
	QJsonArray items;
	for (int i = 0; i < m_tree->topLevelItemCount(); i++) items.append(itemToJson(m_tree->topLevelItem(i)));
	QJsonObject root; root["version"] = 1; root["items"] = items;
	QJsonDocument doc(root);
	QFile file(configPath());
	if (file.open(QIODevice::WriteOnly)) file.write(doc.toJson(QJsonDocument::Indented));
}

void SceneOrganiserDock::load()
{
	m_inhibit = true;
	m_tree->clear();
	QFile file(configPath());
	if (file.exists() && file.open(QIODevice::ReadOnly)) {
		QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
		if (doc.isObject()) itemsFromJson(nullptr, doc.object()["items"].toArray());
	}
	m_inhibit = false;
	syncScenes();
}

QJsonObject SceneOrganiserDock::itemToJson(QTreeWidgetItem *item) const
{
	QJsonObject obj;
	QColor color = item->data(0, RoleColor).value<QColor>();
	if (item->type() == OrgFolder) {
		obj["type"] = "folder"; obj["name"] = item->text(0);
		obj["color"] = color.isValid() ? color.name() : ""; obj["expanded"] = item->isExpanded();
		QJsonArray children; for (int i = 0; i < item->childCount(); i++) children.append(itemToJson(item->child(i)));
		obj["children"] = children;
	} else if (item->type() == OrgSeparator) {
		obj["type"] = "separator"; obj["color"] = color.isValid() ? color.name() : "";
	} else if (item->type() == OrgTextField) {
		obj["type"] = "textfield"; obj["text"] = item->text(0); obj["color"] = color.isValid() ? color.name() : "";
	} else {
		obj["type"] = "scene"; obj["name"] = item->data(0, RoleObsName).toString(); obj["color"] = color.isValid() ? color.name() : "";
	}
	return obj;
}

void SceneOrganiserDock::itemsFromJson(QTreeWidgetItem *parent, const QJsonArray &arr)
{
	for (const QJsonValue &val : arr) {
		QJsonObject obj = val.toObject();
		QString type = obj["type"].toString();
		if (type == "folder") {
			QTreeWidgetItem *it = makeFolderItem(parent, obj["name"].toString());
			QString cs = obj["color"].toString();
			if (!cs.isEmpty()) applyColor(it, QColor(cs));
			it->setExpanded(obj["expanded"].toBool(true));
			itemsFromJson(it, obj["children"].toArray());
		} else if (type == "separator") {
			QTreeWidgetItem *it = makeSeparatorItem(parent);
			QString cs = obj["color"].toString();
			if (!cs.isEmpty()) it->setData(0, RoleColor, QColor(cs));
		} else if (type == "textfield") {
			QTreeWidgetItem *it = makeTextFieldItem(parent, obj["text"].toString());
			QString cs = obj["color"].toString();
			if (!cs.isEmpty()) applyColor(it, QColor(cs));
		} else if (type == "scene") {
			QTreeWidgetItem *it = makeSceneItem(parent, obj["name"].toString());
			QString cs = obj["color"].toString();
			if (!cs.isEmpty()) applyColor(it, QColor(cs));
		}
	}
}

extern "C" void scene_organiser_register_dock(void)
{
	auto *dock = new SceneOrganiserDock();
	obs_frontend_add_dock_by_id("obs-scene-organiser-dock", obs_module_text("Organiser.Dock.Title"), dock);
}