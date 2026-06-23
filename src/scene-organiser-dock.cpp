/*
 * obs-scene-organiser
 * Copyright (C) 2026 K_STYER1
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. See LICENSE for details.
 */
#include "scene-organiser-dock.hpp"
#include "plugin-support.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QDropEvent>
#include <QPainter>
#include <QPen>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QJsonParseError>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QFont>
#include <QFontMetrics>
#include <QMainWindow>
#include <QListWidget>
#include <QApplication>
#include <QContextMenuEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QStyle>
#include <QToolButton>
#include <algorithm>

static const char *DOCK_ID = "obs-scene-organiser-dock";
static SceneOrganiserDock *g_dock = nullptr;
static bool g_dock_registered = false;
static bool g_frontend_api_closed = false;
static void scene_organiser_cleanup_dock(bool frontendAvailable);

/* ================================================================== */
/* OrgTree                                                             */
/* ================================================================== */

void OrgTree::dropEvent(QDropEvent *e)
{
	QTreeWidgetItem *target = itemAt(e->position().toPoint());
	DropIndicatorPosition indicator = dropIndicatorPosition();

	QList<QTreeWidgetItem *> dragged = selectedItems();
	bool draggingFolder = std::any_of(dragged.begin(), dragged.end(),
					  [](QTreeWidgetItem *i) {
						  return i->type() == OrgFolder;
					  });

	if (draggingFolder && target && target->type() == OrgFolder &&
	    indicator == OnItem) {
		e->ignore();
		return;
	}

	QTreeWidget::dropEvent(e);
	emit itemDropped();
}

/* ================================================================== */
/* ColorBarDelegate                                                    */
/* ================================================================== */

void ColorBarDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
			     const QModelIndex &idx) const
{
	/* Check if this row is a separator */
	const auto *tw =
		static_cast<const QTreeWidget *>(parent());
	QTreeWidgetItem *item = tw->itemFromIndex(idx);

	if (item && item->type() == OrgSeparator) {
		p->fillRect(opt.rect, opt.palette.window());
		QColor c = idx.data(RoleColor).value<QColor>();
		if (!c.isValid())
			c = QColor(110, 110, 110);
		int y = opt.rect.top() + opt.rect.height() / 2;
		p->fillRect(opt.rect.left() + 6, y - 1,
			    qMax(0, opt.rect.width() - 12), 2, c);
		return;
	}

	bool searchBlink = idx.data(RoleSearchBlink).toBool();
	QStyleOptionViewItem paintOpt(opt);
	if (searchBlink)
		paintOpt.backgroundBrush = QBrush(QColor(255, 214, 64, 95));

	QStyledItemDelegate::paint(p, paintOpt, idx);

	if (searchBlink) {
		p->save();
		QColor line(255, 193, 7);
		QPen pen(line);
		pen.setWidth(2);
		p->setPen(pen);
		p->drawRect(opt.rect.adjusted(1, 1, -2, -2));
		p->restore();
	}

	QColor color = idx.data(RoleColor).value<QColor>();
	if (color.isValid()) {
		/* x=0 in viewport coords = left edge of the widget,
		   always to the left of branch indicators and icons */
		p->fillRect(0, opt.rect.top(), 4, opt.rect.height(), color);
	}
}

/* ================================================================== */
/* Helpers                                                             */
/* ================================================================== */

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
	/* tab */
	p.drawRoundedRect(QRectF(1.5, 3.0, 6.0, 3.5), 1.0, 1.0);
	/* body */
	p.drawRoundedRect(QRectF(1.5, 5.0, 13.0, 9.5), 1.5, 1.5);
	return QIcon(pm);
}

struct SourceSearchContext {
	QString needle;
	bool matched = false;
	QSet<obs_scene_t *> visitedScenes;
};

static bool sourceSearchScene(obs_scene_t *scene, SourceSearchContext *ctx);

static bool sourceSearchItemCallback(obs_scene_t *, obs_sceneitem_t *item,
				     void *data)
{
	auto *ctx = static_cast<SourceSearchContext *>(data);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;

	const char *name = obs_source_get_name(source);
	if (name && QString::fromUtf8(name).toLower().contains(ctx->needle)) {
		ctx->matched = true;
		return false;
	}

	obs_scene_t *nested = obs_group_or_scene_from_source(source);
	if (nested && sourceSearchScene(nested, ctx))
		return false;

	return true;
}

static bool sourceSearchScene(obs_scene_t *scene, SourceSearchContext *ctx)
{
	if (!scene || ctx->visitedScenes.contains(scene))
		return ctx->matched;

	ctx->visitedScenes.insert(scene);
	obs_scene_enum_items(scene, sourceSearchItemCallback, ctx);
	return ctx->matched;
}

/* ================================================================== */
/* SceneOrganiserDock                                                  */
/* ================================================================== */

SceneOrganiserDock::SceneOrganiserDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("SceneOrganiserDock");

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	/* ---- top bar (search only) ---- */
	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(obs_module_text("Organiser.Search.Placeholder"));
	m_search->setClearButtonEnabled(true);
	root->addWidget(m_search);

	/* ---- tree ---- */
	m_tree = new OrgTree();
	m_tree->setHeaderHidden(true);
	m_tree->setDragDropMode(QAbstractItemView::InternalMove);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	m_tree->setItemDelegate(new ColorBarDelegate(m_tree));
	m_tree->setStyleSheet(
		"QTreeWidget::item { padding-left: 4px; min-height: 22px; }");
	root->addWidget(m_tree, 1);

	/* ---- bottom toolbar ---- */
	auto *bottomBar = new QHBoxLayout();
	bottomBar->setSpacing(2);
	bottomBar->setContentsMargins(0, 0, 0, 0);

	auto makeBtn = [this](QStyle::StandardPixmap iconType,
			      const QString &fallback, const QString &tip) {
		auto *b = new QToolButton(this);
		QIcon icon = style()->standardIcon(iconType);
		b->setText(fallback);
		if (!icon.isNull())
			b->setIcon(icon);
		b->setToolTip(tip);
		b->setAccessibleName(tip);
		b->setToolButtonStyle(Qt::ToolButtonIconOnly);
		b->setFixedSize(26, 24);
		b->setAutoRaise(true);
		QFont f = b->font();
		f.setPointSize(11);
		f.setBold(true);
		b->setFont(f);
		return b;
	};

	auto *addBtn = makeBtn(QStyle::SP_FileDialogNewFolder, "+",
			       obs_module_text("Organiser.Tip.Add"));
	auto *delBtn = makeBtn(QStyle::SP_TrashIcon, QString::fromUtf8("\u2212"),
			       obs_module_text("Organiser.Tip.Delete"));
	auto *upBtn = makeBtn(QStyle::SP_ArrowUp, QString::fromUtf8("\u25B2"),
			      obs_module_text("Organiser.Tip.MoveUp"));
	auto *downBtn = makeBtn(QStyle::SP_ArrowDown,
				QString::fromUtf8("\u25BC"),
				obs_module_text("Organiser.Tip.MoveDown"));

	bottomBar->addWidget(addBtn);
	bottomBar->addWidget(delBtn);
	bottomBar->addWidget(upBtn);
	bottomBar->addWidget(downBtn);
	bottomBar->addStretch(1);
	root->addLayout(bottomBar);

	/* ---- connections ---- */
	connect(addBtn, &QToolButton::clicked, this,
		&SceneOrganiserDock::addItem);
	connect(delBtn, &QToolButton::clicked, this,
		&SceneOrganiserDock::deleteSelected);
	connect(upBtn, &QToolButton::clicked, this,
		[this] { moveSelected(-1); });
	connect(downBtn, &QToolButton::clicked, this,
		[this] { moveSelected(+1); });

	/* ---- shortcuts ---- */
	auto *upSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), m_tree);
	upSc->setContext(Qt::WidgetWithChildrenShortcut);
	connect(upSc, &QShortcut::activated, this,
		[this] { moveSelected(-1); });

	auto *downSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down),
				     m_tree);
	downSc->setContext(Qt::WidgetWithChildrenShortcut);
	connect(downSc, &QShortcut::activated, this,
		[this] { moveSelected(+1); });

	auto *delSc = new QShortcut(QKeySequence::Delete, m_tree);
	delSc->setContext(Qt::WidgetWithChildrenShortcut);
	connect(delSc, &QShortcut::activated, this,
		&SceneOrganiserDock::deleteSelected);

	connect(m_search, &QLineEdit::textChanged, this,
		&SceneOrganiserDock::onSearchChanged);
	connect(m_tree, &QTreeWidget::itemClicked, this,
		&SceneOrganiserDock::onItemClicked);
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
		&SceneOrganiserDock::onItemDoubleClicked);
	connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
		&SceneOrganiserDock::onContextMenu);
	connect(m_tree, &QTreeWidget::itemChanged, this,
		&SceneOrganiserDock::onItemChanged);
	connect(m_tree, &OrgTree::itemDropped, this,
		&SceneOrganiserDock::onItemDropped);

	m_searchBlinkTimer = new QTimer(this);
	m_searchBlinkTimer->setInterval(140);
	connect(m_searchBlinkTimer, &QTimer::timeout, this,
		&SceneOrganiserDock::advanceSearchBlink);

}

SceneOrganiserDock::~SceneOrganiserDock()
{
	PrepareShutdown(!g_frontend_api_closed);
	if (g_dock == this) {
		g_dock = nullptr;
		g_dock_registered = false;
	}
}

void SceneOrganiserDock::PrepareShutdown(bool fromObsExit)
{
	if (m_shutdownPrepared)
		return;
	m_shutdownPrepared = true;

	clearSearchBlink();
	m_loaded = false;

	if (fromObsExit && m_frontendCallbackRegistered) {
		obs_frontend_remove_event_callback(frontendEvent, this);
		m_frontendCallbackRegistered = false;
	} else if (!fromObsExit) {
		m_frontendCallbackRegistered = false;
	}

	if (fromObsExit && m_sourceRenameSignalRegistered) {
		signal_handler_t *handler = obs_get_signal_handler();
		if (handler)
			signal_handler_disconnect(handler, "source_rename",
						  sourceRenameSignal, this);
		m_sourceRenameSignalRegistered = false;
	} else if (!fromObsExit) {
		m_sourceRenameSignalRegistered = false;
	}

}

void SceneOrganiserDock::RegisterFrontendCallback()
{
	if (m_frontendCallbackRegistered)
		return;

	obs_frontend_add_event_callback(frontendEvent, this);
	m_frontendCallbackRegistered = true;

	signal_handler_t *handler = obs_get_signal_handler();
	if (handler) {
		signal_handler_connect(handler, "source_rename",
				       sourceRenameSignal, this);
		m_sourceRenameSignalRegistered = true;
	}
}

/* ------------------------------------------------------------------ */
/* OBS event dispatch                                                  */
/* ------------------------------------------------------------------ */

void SceneOrganiserDock::frontendEvent(obs_frontend_event event, void *data)
{
	auto *self = static_cast<SceneOrganiserDock *>(data);
	if (!self)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_EXIT:
		scene_organiser_cleanup_dock(true);
		g_frontend_api_closed = true;
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (self->m_shutdownPrepared)
			break;
		QMetaObject::invokeMethod(self, "onFinishedLoading",
					  Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		if (self->m_shutdownPrepared)
			break;
		QMetaObject::invokeMethod(self, "syncScenes",
					  Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		if (self->m_shutdownPrepared)
			break;
		QMetaObject::invokeMethod(self, "highlightCurrentScene",
					  Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (self->m_shutdownPrepared)
			break;
		QMetaObject::invokeMethod(self, "onCollectionChanged",
					  Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_RENAMED:
		if (self->m_shutdownPrepared)
			break;
		QMetaObject::invokeMethod(self, "onCollectionRenamed",
					  Qt::QueuedConnection);
		break;
	default:
		break;
	}
}

void SceneOrganiserDock::sourceRenameSignal(void *data, calldata_t *cd)
{
	auto *self = static_cast<SceneOrganiserDock *>(data);
	if (!self || self->m_shutdownPrepared)
		return;

	const char *prevRaw = calldata_string(cd, "prev_name");
	const char *newRaw = calldata_string(cd, "new_name");
	if (!prevRaw || !newRaw)
		return;

	QString prevName = QString::fromUtf8(prevRaw);
	QString newName = QString::fromUtf8(newRaw);
	QMetaObject::invokeMethod(
		self,
		[self, prevName, newName] {
			if (!self->m_shutdownPrepared)
				self->onSourceRenamed(prevName, newName);
		},
		Qt::QueuedConnection);
}

/* ------------------------------------------------------------------ */
/* OBS sync slots                                                      */
/* ------------------------------------------------------------------ */

void SceneOrganiserDock::onFinishedLoading()
{
	if (m_shutdownPrepared)
		return;

	if (!load())
		return;
	m_loaded = true;
	syncScenes();
}

void SceneOrganiserDock::onCollectionChanged()
{
	if (m_shutdownPrepared)
		return;

	m_loaded = false;
	if (!load())
		return;
	m_loaded = true;
	syncScenes();
}

void SceneOrganiserDock::onCollectionRenamed()
{
	if (m_shutdownPrepared)
		return;

	QString newPath = configPath();
	if (!newPath.isEmpty() && !m_lastConfigPath.isEmpty() &&
	    m_lastConfigPath != newPath && QFile::exists(m_lastConfigPath) &&
	    !QFile::exists(newPath)) {
		if (!QFile::copy(m_lastConfigPath, newPath)) {
			obs_log(LOG_WARNING,
				"could not migrate scene collection config %s to %s",
				m_lastConfigPath.toUtf8().constData(),
				newPath.toUtf8().constData());
		}
	}

	onCollectionChanged();
}

void SceneOrganiserDock::syncScenes()
{
	if (m_shutdownPrepared || !m_loaded)
		return;

	m_inhibit = true;

	struct obs_frontend_source_list list = {0};
	obs_frontend_get_scenes(&list);
	QSet<QString> obsNames;
	for (size_t i = 0; i < list.sources.num; i++)
		obsNames.insert(
			QString::fromUtf8(obs_source_get_name(list.sources.array[i])));
	obs_frontend_source_list_free(&list);

	QSet<QString> treeNames;
	collectObsNames(nullptr, treeNames);

	for (const QString &name : obsNames) {
		if (!treeNames.contains(name))
			makeSceneItem(nullptr, name);
	}

	removeOrphans(nullptr, obsNames);

	m_inhibit = false;

	highlightCurrentScene();
	save();
}

void SceneOrganiserDock::highlightCurrentScene()
{
	if (m_shutdownPrepared)
		return;

	obs_source_t *src = obs_frontend_get_current_scene();
	QString name = src ? QString::fromUtf8(obs_source_get_name(src)) : "";
	if (src)
		obs_source_release(src);

	highlightItems(nullptr, name);
}

/* ------------------------------------------------------------------ */
/* Tree helpers                                                        */
/* ------------------------------------------------------------------ */

QTreeWidgetItem *SceneOrganiserDock::makeSceneItem(QTreeWidgetItem *parent,
						   const QString &name)
{
	QTreeWidgetItem *item;
	if (parent)
		item = new QTreeWidgetItem(parent, OrgScene);
	else
		item = new QTreeWidgetItem(m_tree, OrgScene);

	item->setText(0, name);
	item->setData(0, RoleObsName, name);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
		       Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
	return item;
}

QTreeWidgetItem *SceneOrganiserDock::makeFolderItem(QTreeWidgetItem *parent,
						    const QString &name)
{
	QTreeWidgetItem *item;
	if (parent)
		item = new QTreeWidgetItem(parent, OrgFolder);
	else
		item = new QTreeWidgetItem(m_tree, OrgFolder);

	item->setText(0, name);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
		       Qt::ItemIsEditable | Qt::ItemIsDragEnabled |
		       Qt::ItemIsDropEnabled);
	item->setExpanded(true);
	item->setIcon(0, makeFolderIcon(QColor()));
	return item;
}

QTreeWidgetItem *SceneOrganiserDock::makeTextFieldItem(QTreeWidgetItem *parent,
						       const QString &text)
{
	QTreeWidgetItem *item;
	if (parent)
		item = new QTreeWidgetItem(parent, OrgTextField);
	else
		item = new QTreeWidgetItem(m_tree, OrgTextField);

	item->setText(0, text);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
		       Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

	QFont f = item->font(0);
	f.setItalic(true);
	item->setFont(0, f);
	return item;
}

QTreeWidgetItem *SceneOrganiserDock::makeSeparatorItem(QTreeWidgetItem *parent)
{
	QTreeWidgetItem *item;
	if (parent)
		item = new QTreeWidgetItem(parent, OrgSeparator);
	else
		item = new QTreeWidgetItem(m_tree, OrgSeparator);

	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
		       Qt::ItemIsDragEnabled);
	item->setSizeHint(0, QSize(0, 22));
	return item;
}

void SceneOrganiserDock::collectObsNames(QTreeWidgetItem *parent,
					 QSet<QString> &out) const
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child =
			parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene)
			out.insert(child->data(0, RoleObsName).toString());
		else if (child->type() == OrgFolder)
			collectObsNames(child, out);
	}
}

void SceneOrganiserDock::removeOrphans(QTreeWidgetItem *parent,
				       const QSet<QString> &valid)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = n - 1; i >= 0; i--) {
		QTreeWidgetItem *child =
			parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene) {
			if (!valid.contains(child->data(0, RoleObsName).toString()))
				delete child;
		} else if (child->type() == OrgFolder) {
			removeOrphans(child, valid);
		}
	}
}

void SceneOrganiserDock::highlightItems(QTreeWidgetItem *parent,
					const QString &current)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child =
			parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene) {
			QFont f = child->font(0);
			f.setBold(child->data(0, RoleObsName).toString() ==
				  current);
			child->setFont(0, f);
		} else if (child->type() == OrgFolder) {
			highlightItems(child, current);
		}
	}
}

void SceneOrganiserDock::applyColor(QTreeWidgetItem *item, const QColor &color)
{
	item->setData(0, RoleColor, color);
	QColor bg = color;
	bg.setAlpha(45);
	item->setBackground(0, bg);
	if (item->type() == OrgFolder)
		item->setIcon(0, makeFolderIcon(color));
}

bool SceneOrganiserDock::filterItems(QTreeWidgetItem *parent,
				     const QString &text,
				     QSet<QString> *sourceMatches)
{
	bool anyVisible = false;
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child =
			parent ? parent->child(i) : m_tree->topLevelItem(i);

		if (child->type() == OrgScene) {
			QString sceneName =
				child->data(0, RoleObsName).toString();
			bool nameMatch = text.isEmpty() ||
					 child->text(0).toLower().contains(text);
			bool sourceMatch =
				!text.isEmpty() &&
				sceneContainsSource(sceneName, text);
			bool visible = nameMatch || sourceMatch;
			child->setHidden(!visible);
			child->setData(0, RoleSearchBlink, false);
			if (sourceMatch && sourceMatches)
				sourceMatches->insert(sceneName);
			anyVisible = anyVisible || visible;
		} else if (child->type() == OrgFolder) {
			bool childVisible =
				filterItems(child, text, sourceMatches);
			bool folderMatch = !text.isEmpty() &&
					   child->text(0).toLower().contains(text);
			bool visible = text.isEmpty() || childVisible || folderMatch;
			child->setHidden(!visible);
			if (!text.isEmpty())
				child->setExpanded(childVisible || folderMatch);
			anyVisible = anyVisible || visible;
		} else {
			bool textFieldMatch =
				child->type() == OrgTextField &&
				child->text(0).toLower().contains(text);
			bool visible = text.isEmpty() || textFieldMatch;
			child->setHidden(!visible);
			child->setData(0, RoleSearchBlink, false);
			anyVisible = anyVisible || visible;
		}
	}

	return anyVisible;
}

bool SceneOrganiserDock::sceneContainsSource(const QString &sceneName,
					     const QString &text) const
{
	if (m_shutdownPrepared || text.isEmpty())
		return false;

	obs_source_t *source =
		obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!source)
		return false;

	obs_scene_t *scene = obs_scene_from_source(source);
	SourceSearchContext ctx{text};
	if (scene)
		sourceSearchScene(scene, &ctx);

	obs_source_release(source);
	return ctx.matched;
}

void SceneOrganiserDock::startSearchBlink(const QSet<QString> &sceneNames)
{
	clearSearchBlink();
	if (sceneNames.isEmpty() || !m_searchBlinkTimer)
		return;

	m_searchBlinkSceneNames = sceneNames;
	m_searchBlinkTicks = 0;
	setSearchBlinkVisible(nullptr, true);
	m_searchBlinkTimer->start();
}

void SceneOrganiserDock::clearSearchBlink()
{
	if (m_searchBlinkTimer)
		m_searchBlinkTimer->stop();

	setSearchBlinkVisible(nullptr, false);
	m_searchBlinkSceneNames.clear();
	m_searchBlinkTicks = 0;
}

void SceneOrganiserDock::setSearchBlinkVisible(QTreeWidgetItem *parent,
					       bool visible)
{
	if (!m_tree)
		return;

	bool wasInhibit = m_inhibit;
	m_inhibit = true;

	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child =
			parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene) {
			QString sceneName =
				child->data(0, RoleObsName).toString();
			bool shouldBlink =
				visible &&
				m_searchBlinkSceneNames.contains(sceneName);
			if (!visible || shouldBlink)
				child->setData(0, RoleSearchBlink, shouldBlink);
		} else if (child->type() == OrgFolder) {
			setSearchBlinkVisible(child, visible);
		}
	}

	m_inhibit = wasInhibit;

	if (!parent)
		m_tree->viewport()->update();
}

void SceneOrganiserDock::advanceSearchBlink()
{
	if (m_shutdownPrepared || m_searchBlinkSceneNames.isEmpty()) {
		clearSearchBlink();
		return;
	}

	m_searchBlinkTicks++;
	setSearchBlinkVisible(nullptr, (m_searchBlinkTicks % 2) == 0);
	if (m_searchBlinkTicks >= 7)
		clearSearchBlink();
}

/* ------------------------------------------------------------------ */
/* Slots                                                               */
/* ------------------------------------------------------------------ */

void SceneOrganiserDock::onItemClicked(QTreeWidgetItem *item, int)
{
	if (m_shutdownPrepared)
		return;

	if (item && item->type() == OrgScene)
		switchToScene(item);
}

void SceneOrganiserDock::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
	if (m_shutdownPrepared)
		return;

	if (item && item->type() == OrgScene)
		switchToScene(item);
}

void SceneOrganiserDock::onSearchChanged(const QString &text)
{
	if (m_shutdownPrepared)
		return;

	QString needle = text.trimmed().toLower();
	QSet<QString> sourceMatches;
	bool wasInhibit = m_inhibit;
	m_inhibit = true;
	filterItems(nullptr, needle, &sourceMatches);
	startSearchBlink(sourceMatches);
	m_inhibit = wasInhibit;
}

void SceneOrganiserDock::onItemChanged(QTreeWidgetItem *item, int col)
{
	if (m_shutdownPrepared || m_inhibit || col != 0)
		return;

	if (item->type() == OrgScene) {
		QString newName = item->text(0).trimmed();
		QString oldName = item->data(0, RoleObsName).toString();
		if (newName.isEmpty()) {
			m_inhibit = true;
			item->setText(0, oldName);
			m_inhibit = false;
			return;
		}

		if (newName != oldName) {
			obs_source_t *src = obs_get_source_by_name(
				oldName.toUtf8().constData());
			if (src) {
				obs_source_set_name(
					src, newName.toUtf8().constData());
				const char *actualName = obs_source_get_name(src);
				newName = actualName ? QString::fromUtf8(actualName)
						     : oldName;
				obs_source_release(src);
			} else {
				newName = oldName;
			}

			m_inhibit = true;
			item->setText(0, newName);
			item->setData(0, RoleObsName, newName);
			m_inhibit = false;
		} else if (item->text(0) != newName) {
			m_inhibit = true;
			item->setText(0, newName);
			m_inhibit = false;
		}
	}
	save();
}

void SceneOrganiserDock::onItemDropped()
{
	if (m_shutdownPrepared)
		return;

	save();
}

void SceneOrganiserDock::onContextMenu(const QPoint &pos)
{
	if (m_shutdownPrepared)
		return;

	QTreeWidgetItem *item = m_tree->itemAt(pos);
	QPersistentModelIndex itemIndex =
		item ? QPersistentModelIndex(m_tree->indexFromItem(item))
		     : QPersistentModelIndex();
	auto currentItem = [this, itemIndex]() -> QTreeWidgetItem * {
		return itemIndex.isValid() ? m_tree->itemFromIndex(itemIndex)
					   : nullptr;
	};
	QMenu menu;

	if (item && item->type() == OrgSeparator) {
		menu.addAction(
			obs_module_text("Organiser.Action.DuplicateSeparator"),
			this, [this, currentItem] {
				if (auto *current = currentItem())
					duplicateSeparator(current);
			});
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       pickColor(current);
			       });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       clearColor(current);
			       });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.DeleteSeparator"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       deleteSeparator(current);
			       });

	} else if (item && item->type() == OrgTextField) {
		menu.addAction(obs_module_text("Organiser.Action.RenameTextField"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       renameItem(current);
			       });
		menu.addAction(
			obs_module_text("Organiser.Action.DuplicateTextField"),
			this, [this, currentItem] {
				if (auto *current = currentItem())
					duplicateTextField(current);
			});
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       pickColor(current);
			       });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       clearColor(current);
			       });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.DeleteTextField"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       deleteTextField(current);
			       });

	} else if (!item) {
		menu.addAction(obs_module_text("Organiser.Action.AddFolder"),
			       this, [this] { addFolder(nullptr); });
		menu.addAction(obs_module_text("Organiser.Action.AddSeparator"),
			       this, &SceneOrganiserDock::addSeparator);
		menu.addAction(obs_module_text("Organiser.Action.AddTextField"),
			       this, &SceneOrganiserDock::addTextField);
		QMenu *sortMenu = menu.addMenu(
			obs_module_text("Organiser.Action.Sort"));
		sortMenu->addAction(
			obs_module_text("Organiser.Action.Sort.AZ"), this,
			[this] { sortChildren(nullptr, Qt::AscendingOrder); });
		sortMenu->addAction(
			obs_module_text("Organiser.Action.Sort.ZA"), this,
			[this] { sortChildren(nullptr, Qt::DescendingOrder); });

	} else if (item->type() == OrgFolder) {
		menu.addAction(obs_module_text("Organiser.Action.AddFolder"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       addFolder(current);
			       });
		menu.addAction(obs_module_text("Organiser.Action.RenameFolder"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       renameItem(current);
			       });
		menu.addAction(obs_module_text("Organiser.Action.DeleteFolder"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       deleteFolder(current);
			       });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       pickColor(current);
			       });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       clearColor(current);
			       });
		menu.addSeparator();
		QMenu *sortMenu = menu.addMenu(
			obs_module_text("Organiser.Action.Sort"));
		sortMenu->addAction(
			obs_module_text("Organiser.Action.Sort.AZ"), this,
			[this, currentItem] {
				if (auto *current = currentItem())
					sortChildren(current, Qt::AscendingOrder);
			});
		sortMenu->addAction(
			obs_module_text("Organiser.Action.Sort.ZA"), this,
			[this, currentItem] {
				if (auto *current = currentItem())
					sortChildren(current, Qt::DescendingOrder);
			});

	} else if (item->type() == OrgScene) {
		menu.addAction(
			obs_module_text("Organiser.Action.SwitchToScene"),
			this, [this, currentItem] {
				if (auto *current = currentItem())
					switchToScene(current);
			});
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.RenameScene"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       renameItem(current);
			       });
		menu.addAction(
			obs_module_text("Organiser.Action.DuplicateScene"),
			this, [this, currentItem] {
				if (auto *current = currentItem())
					duplicateScene(current);
			});
		menu.addAction(obs_module_text("Organiser.Action.RemoveScene"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       removeScene(current);
			       });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.OpenFilters"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       openFilters(current);
			       });
		menu.addSeparator();
		menu.addAction(obs_module_text("Organiser.Action.SetColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       pickColor(current);
			       });
		menu.addAction(obs_module_text("Organiser.Action.ClearColor"),
			       this, [this, currentItem] {
				       if (auto *current = currentItem())
					       clearColor(current);
			       });

		/* Forward OBS' native scene context menu */
		QString sceneName = item->data(0, RoleObsName).toString();
		auto *mw = static_cast<QMainWindow *>(
			obs_frontend_get_main_window());
		QListWidget *obsScenes =
			mw ? mw->findChild<QListWidget *>("scenes") : nullptr;
		if (obsScenes) {
			menu.addSeparator();
			menu.addAction(
				obs_module_text("Organiser.Action.NativeMenu"),
				this, [obsScenes, sceneName] {
					for (int i = 0; i < obsScenes->count();
					     i++) {
						if (obsScenes->item(i)->text() ==
						    sceneName) {
							obsScenes->setCurrentRow(i);
							break;
						}
					}
					QListWidgetItem *cur =
						obsScenes->currentItem();
					if (!cur)
						return;
					QPoint p = obsScenes
							   ->visualItemRect(cur)
							   .center();
					QContextMenuEvent ev(
						QContextMenuEvent::Mouse, p,
						obsScenes->viewport()
							->mapToGlobal(p));
					QApplication::sendEvent(
						obsScenes->viewport(), &ev);
				});
		}
	}

	if (!menu.actions().isEmpty())
		menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

void SceneOrganiserDock::addItem()
{
	QMenu menu;
	menu.addAction(obs_module_text("Organiser.Action.AddFolder"), this,
		       [this] { addFolder(); });
	menu.addAction(obs_module_text("Organiser.Action.AddSeparator"), this,
		       &SceneOrganiserDock::addSeparator);
	menu.addAction(obs_module_text("Organiser.Action.AddTextField"), this,
		       &SceneOrganiserDock::addTextField);

	auto *btn = qobject_cast<QToolButton *>(sender());
	QPoint pos = btn ? btn->mapToGlobal(QPoint(0, btn->height())) : QCursor::pos();
	menu.exec(pos);
}

void SceneOrganiserDock::addFolder()
{
	addFolder(nullptr);
}

void SceneOrganiserDock::addFolder(QTreeWidgetItem *parent)
{
	QPointer<SceneOrganiserDock> guard(this);
	QPersistentModelIndex parentIndex =
		parent ? QPersistentModelIndex(m_tree->indexFromItem(parent))
		       : QPersistentModelIndex();
	bool ok;
	QString name = QInputDialog::getText(
		nullptr, obs_module_text("Organiser.Dialog.AddFolder.Title"),
		obs_module_text("Organiser.Dialog.AddFolder.Text"),
		QLineEdit::Normal, "", &ok);
	if (!guard || m_shutdownPrepared)
		return;
	if (!ok || name.trimmed().isEmpty())
		return;
	if (parentIndex.isValid())
		parent = m_tree->itemFromIndex(parentIndex);
	else if (parent)
		return;

	makeFolderItem(parent, name.trimmed());
	save();
}

void SceneOrganiserDock::addSeparator()
{
	makeSeparatorItem(nullptr);
	save();
}

void SceneOrganiserDock::deleteSeparator(QTreeWidgetItem *item)
{
	if (item->type() != OrgSeparator)
		return;
	delete item;
	save();
}

/* Insert a new sibling directly after the source item, preserving level */
static void insertSiblingAfter(QTreeWidget *tree, QTreeWidgetItem *src,
			       QTreeWidgetItem *neu)
{
	QTreeWidgetItem *parent = src->parent();
	int idx = parent ? parent->indexOfChild(src)
			 : tree->indexOfTopLevelItem(src);
	if (parent) {
		parent->removeChild(neu);
		parent->insertChild(idx + 1, neu);
	} else {
		int newIdx = tree->indexOfTopLevelItem(neu);
		if (newIdx >= 0)
			tree->takeTopLevelItem(newIdx);
		tree->insertTopLevelItem(idx + 1, neu);
	}
}

void SceneOrganiserDock::duplicateSeparator(QTreeWidgetItem *item)
{
	if (item->type() != OrgSeparator)
		return;
	QTreeWidgetItem *neu = makeSeparatorItem(item->parent());
	QColor c = item->data(0, RoleColor).value<QColor>();
	if (c.isValid())
		neu->setData(0, RoleColor, c);
	insertSiblingAfter(m_tree, item, neu);
	m_tree->setCurrentItem(neu);
	save();
}

void SceneOrganiserDock::duplicateTextField(QTreeWidgetItem *item)
{
	if (item->type() != OrgTextField)
		return;
	QTreeWidgetItem *neu =
		makeTextFieldItem(item->parent(), item->text(0));
	QColor c = item->data(0, RoleColor).value<QColor>();
	if (c.isValid())
		applyColor(neu, c);
	insertSiblingAfter(m_tree, item, neu);
	m_tree->setCurrentItem(neu);
	save();
}

void SceneOrganiserDock::deleteSelected()
{
	QTreeWidgetItem *item = m_tree->currentItem();
	if (!item)
		return;

	switch (item->type()) {
	case OrgFolder:    deleteFolder(item);    break;
	case OrgScene:     removeScene(item);     break;
	case OrgSeparator: deleteSeparator(item); break;
	case OrgTextField: deleteTextField(item); break;
	}
}

bool SceneOrganiserDock::replaceObsName(QTreeWidgetItem *parent,
					const QString &prevName,
					const QString &newName)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	for (int i = 0; i < n; i++) {
		QTreeWidgetItem *child =
			parent ? parent->child(i) : m_tree->topLevelItem(i);
		if (child->type() == OrgScene &&
		    child->data(0, RoleObsName).toString() == prevName) {
			child->setText(0, newName);
			child->setData(0, RoleObsName, newName);
			return true;
		}
		if (child->type() == OrgFolder &&
		    replaceObsName(child, prevName, newName))
			return true;
	}
	return false;
}

void SceneOrganiserDock::onSourceRenamed(const QString &prevName,
					 const QString &newName)
{
	if (m_shutdownPrepared || !m_loaded || prevName.isEmpty() ||
	    newName.isEmpty() || prevName == newName)
		return;

	bool wasInhibit = m_inhibit;
	m_inhibit = true;
	bool changed = replaceObsName(nullptr, prevName, newName);
	m_inhibit = wasInhibit;

	if (changed) {
		highlightCurrentScene();
		save();
	}
}

void SceneOrganiserDock::moveSelected(int direction)
{
	QTreeWidgetItem *item = m_tree->currentItem();
	if (!item || direction == 0)
		return;

	QTreeWidgetItem *parent = item->parent();
	int idx = parent ? parent->indexOfChild(item)
			 : m_tree->indexOfTopLevelItem(item);
	int count = parent ? parent->childCount()
			   : m_tree->topLevelItemCount();
	int newIdx = idx + direction;
	if (newIdx < 0 || newIdx >= count)
		return;

	m_inhibit = true;
	bool wasExpanded = item->isExpanded();
	if (parent) {
		QTreeWidgetItem *taken = parent->takeChild(idx);
		parent->insertChild(newIdx, taken);
	} else {
		QTreeWidgetItem *taken = m_tree->takeTopLevelItem(idx);
		m_tree->insertTopLevelItem(newIdx, taken);
	}
	item->setExpanded(wasExpanded);
	m_tree->setCurrentItem(item);
	m_inhibit = false;
	save();
}

void SceneOrganiserDock::addTextField()
{
	QPointer<SceneOrganiserDock> guard(this);
	bool ok;
	QString text = QInputDialog::getText(
		nullptr, obs_module_text("Organiser.Dialog.AddTextField.Title"),
		obs_module_text("Organiser.Dialog.AddTextField.Text"),
		QLineEdit::Normal, "", &ok);
	if (!guard || m_shutdownPrepared)
		return;
	if (!ok || text.trimmed().isEmpty())
		return;

	makeTextFieldItem(nullptr, text.trimmed());
	save();
}

void SceneOrganiserDock::deleteTextField(QTreeWidgetItem *item)
{
	if (item->type() != OrgTextField)
		return;
	delete item;
	save();
}

void SceneOrganiserDock::renameItem(QTreeWidgetItem *item)
{
	m_tree->editItem(item, 0);
}

void SceneOrganiserDock::deleteFolder(QTreeWidgetItem *item)
{
	if (item->type() != OrgFolder)
		return;

	QTreeWidgetItem *parent = item->parent();
	int idx = parent ? parent->indexOfChild(item)
			 : m_tree->indexOfTopLevelItem(item);
	while (item->childCount() > 0) {
		QTreeWidgetItem *child = item->takeChild(0);
		if (parent)
			parent->insertChild(idx++, child);
		else
			m_tree->insertTopLevelItem(idx++, child);
	}
	delete item;
	save();
}

void SceneOrganiserDock::pickColor(QTreeWidgetItem *item)
{
	QPointer<SceneOrganiserDock> guard(this);
	QPersistentModelIndex itemIndex = m_tree->indexFromItem(item);
	QColor initial = item->data(0, RoleColor).value<QColor>();
	QColor color = QColorDialog::getColor(
		initial.isValid() ? initial : Qt::red, nullptr,
		obs_module_text("Organiser.Action.SetColor"));
	if (!guard || m_shutdownPrepared || !itemIndex.isValid())
		return;
	item = m_tree->itemFromIndex(itemIndex);
	if (!item)
		return;
	if (color.isValid()) {
		applyColor(item, color);
		save();
	}
}

void SceneOrganiserDock::clearColor(QTreeWidgetItem *item)
{
	item->setData(0, RoleColor, QColor());
	item->setBackground(0, QBrush());
	if (item->type() == OrgFolder)
		item->setIcon(0, makeFolderIcon(QColor()));
	save();
}

void SceneOrganiserDock::switchToScene(QTreeWidgetItem *item)
{
	if (m_shutdownPrepared)
		return;

	if (item->type() != OrgScene)
		return;
	QString name = item->data(0, RoleObsName).toString();
	obs_source_t *src =
		obs_get_source_by_name(name.toUtf8().constData());
	if (!src)
		return;

	if (obs_frontend_preview_program_mode_active())
		obs_frontend_set_current_preview_scene(src);
	else
		obs_frontend_set_current_scene(src);

	obs_source_release(src);
}

void SceneOrganiserDock::duplicateScene(QTreeWidgetItem *item)
{
	if (m_shutdownPrepared)
		return;

	if (item->type() != OrgScene)
		return;
	QString name = item->data(0, RoleObsName).toString();
	obs_source_t *src =
		obs_get_source_by_name(name.toUtf8().constData());
	if (!src)
		return;

	obs_scene_t *scene = obs_scene_from_source(src);
	if (!scene) {
		obs_source_release(src);
		return;
	}

	QString newName = name + " (copy)";
	int idx = 2;
	while (true) {
		obs_source_t *existing =
			obs_get_source_by_name(newName.toUtf8().constData());
		if (!existing)
			break;
		obs_source_release(existing);
		newName = name + QString(" (copy %1)").arg(idx++);
	}

	obs_scene_t *dup = obs_scene_duplicate(
		scene, newName.toUtf8().constData(), OBS_SCENE_DUP_REFS);
	if (dup)
		obs_scene_release(dup);

	obs_source_release(src);
}

void SceneOrganiserDock::removeScene(QTreeWidgetItem *item)
{
	if (m_shutdownPrepared)
		return;

	if (item->type() != OrgScene)
		return;
	QString name = item->data(0, RoleObsName).toString();

	QPointer<SceneOrganiserDock> guard(this);
	if (QMessageBox::question(
		    nullptr, obs_module_text("Organiser.Action.RemoveScene"),
		    QString(obs_module_text("Organiser.Dialog.Remove.Text"))
			    .arg(name)) != QMessageBox::Yes)
		return;
	if (!guard || m_shutdownPrepared)
		return;

	obs_source_t *src =
		obs_get_source_by_name(name.toUtf8().constData());
	if (src) {
		obs_source_remove(src);
		obs_source_release(src);
	}
}

void SceneOrganiserDock::openFilters(QTreeWidgetItem *item)
{
	if (m_shutdownPrepared)
		return;

	if (item->type() != OrgScene)
		return;
	QString name = item->data(0, RoleObsName).toString();
	obs_source_t *src =
		obs_get_source_by_name(name.toUtf8().constData());
	if (src) {
		obs_frontend_open_source_filters(src);
		obs_source_release(src);
	}
}

void SceneOrganiserDock::sortChildren(QTreeWidgetItem *parent,
				      Qt::SortOrder order)
{
	int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
	QList<QTreeWidgetItem *> items;
	items.reserve(n);
	for (int i = 0; i < n; i++)
		items.append(parent ? parent->child(i)
				    : m_tree->topLevelItem(i));

	std::sort(items.begin(), items.end(),
		  [order](QTreeWidgetItem *a, QTreeWidgetItem *b) {
			  return order == Qt::AscendingOrder
				       ? a->text(0).toLower() <
						 b->text(0).toLower()
				       : a->text(0).toLower() >
						 b->text(0).toLower();
		  });

	m_inhibit = true;
	if (parent) {
		for (auto *it : items)
			parent->removeChild(it);
		for (auto *it : items)
			parent->addChild(it);
	} else {
		for (auto *it : items)
			m_tree->takeTopLevelItem(
				m_tree->indexOfTopLevelItem(it));
		m_tree->addTopLevelItems(items);
	}
	m_inhibit = false;
	save();
}

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */

QString SceneOrganiserDock::currentSceneCollectionName() const
{
	char *colRaw = obs_frontend_get_current_scene_collection();
	QString col = colRaw ? QString::fromUtf8(colRaw)
			     : QStringLiteral("default");
	bfree(colRaw);
	if (col.trimmed().isEmpty())
		return QStringLiteral("default");
	return col;
}

QString SceneOrganiserDock::configPathForCollection(const QString &collection,
						    bool legacy) const
{
	char *dirRaw = obs_module_config_path("scenes/");
	if (!dirRaw) {
		obs_log(LOG_WARNING, "could not resolve config directory");
		return QString();
	}
	QString dir = QString::fromUtf8(dirRaw);
	bfree(dirRaw);

	if (!QDir().mkpath(dir)) {
		obs_log(LOG_WARNING, "could not create config directory: %s",
			dir.toUtf8().constData());
		return QString();
	}

	QString col = collection;
	col = col.replace(QRegularExpression("[^a-zA-Z0-9_\\- ]"), "_");
	if (col.trimmed().isEmpty())
		col = QStringLiteral("default");
	if (!legacy) {
		QByteArray hash = QCryptographicHash::hash(
						collection.toUtf8(),
						QCryptographicHash::Sha256)
					  .toHex()
					  .left(12);
		col += QStringLiteral("-") + QString::fromLatin1(hash);
	}

	return dir + col + ".json";
}

QString SceneOrganiserDock::configPath() const
{
	return configPathForCollection(currentSceneCollectionName(), false);
}

QString SceneOrganiserDock::legacyConfigPath() const
{
	return configPathForCollection(currentSceneCollectionName(), true);
}

void SceneOrganiserDock::save()
{
	if (m_shutdownPrepared || m_inhibit || !m_loaded)
		return;

	QJsonArray items;
	for (int i = 0; i < m_tree->topLevelItemCount(); i++)
		items.append(itemToJson(m_tree->topLevelItem(i)));

	QJsonObject root;
	root["version"] = 1;
	root["items"] = items;

	QJsonDocument doc(root);
	QString path = configPath();
	if (path.isEmpty())
		return;

	QByteArray json = doc.toJson(QJsonDocument::Indented);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "could not open config for writing: %s",
			path.toUtf8().constData());
		return;
	}
	if (file.write(json) != json.size()) {
		obs_log(LOG_WARNING, "could not write complete config: %s",
			path.toUtf8().constData());
		file.cancelWriting();
		return;
	}
	if (!file.commit())
		obs_log(LOG_WARNING, "could not commit config write: %s",
			path.toUtf8().constData());
	else
		m_lastConfigPath = path;
}

bool SceneOrganiserDock::load()
{
	if (m_shutdownPrepared)
		return false;

	clearSearchBlink();

	QString path = configPath();
	if (path.isEmpty())
		return false;

	QString legacyPath = legacyConfigPath();
	if (!QFile::exists(path) && legacyPath != path && QFile::exists(legacyPath))
		path = legacyPath;

	QJsonArray items;
	QFile file(path);
	if (file.exists() && !file.open(QIODevice::ReadOnly)) {
		obs_log(LOG_WARNING, "could not open config for reading %s: %s",
			path.toUtf8().constData(),
			file.errorString().toUtf8().constData());
		m_inhibit = true;
		m_tree->clear();
		m_inhibit = false;
		return false;
	}
	if (file.isOpen()) {
		QJsonParseError err;
		QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
		if (err.error != QJsonParseError::NoError) {
			obs_log(LOG_WARNING, "could not parse config %s: %s",
				path.toUtf8().constData(),
				err.errorString().toUtf8().constData());
			m_inhibit = true;
			m_tree->clear();
			m_inhibit = false;
			return false;
		}
		if (!doc.isObject() || !doc.object()["items"].isArray()) {
			obs_log(LOG_WARNING, "ignoring unsupported config shape: %s",
				path.toUtf8().constData());
			m_inhibit = true;
			m_tree->clear();
			m_inhibit = false;
			return false;
		}
		items = doc.object()["items"].toArray();
	}

	m_inhibit = true;
	m_tree->clear();
	itemsFromJson(nullptr, items);
	m_inhibit = false;
	m_lastConfigPath = path;
	return true;
}

QJsonObject SceneOrganiserDock::itemToJson(QTreeWidgetItem *item) const
{
	QJsonObject obj;
	QColor color = item->data(0, RoleColor).value<QColor>();

	if (item->type() == OrgFolder) {
		obj["type"] = "folder";
		obj["name"] = item->text(0);
		obj["color"] = color.isValid() ? color.name() : "";
		obj["expanded"] = item->isExpanded();
		QJsonArray children;
		for (int i = 0; i < item->childCount(); i++)
			children.append(itemToJson(item->child(i)));
		obj["children"] = children;
	} else if (item->type() == OrgSeparator) {
		obj["type"] = "separator";
		obj["color"] = color.isValid() ? color.name() : "";
	} else if (item->type() == OrgTextField) {
		obj["type"] = "textfield";
		obj["text"] = item->text(0);
		obj["color"] = color.isValid() ? color.name() : "";
	} else {
		obj["type"] = "scene";
		obj["name"] = item->data(0, RoleObsName).toString();
		obj["color"] = color.isValid() ? color.name() : "";
	}
	return obj;
}

void SceneOrganiserDock::itemsFromJson(QTreeWidgetItem *parent,
				       const QJsonArray &arr)
{
	for (const QJsonValue &val : arr) {
		QJsonObject obj = val.toObject();
		QString type = obj["type"].toString();

		if (type == "folder") {
			QTreeWidgetItem *it =
				makeFolderItem(parent, obj["name"].toString());
			QString cs = obj["color"].toString();
			if (!cs.isEmpty())
				applyColor(it, QColor(cs));
			it->setExpanded(obj["expanded"].toBool(true));
			itemsFromJson(it, obj["children"].toArray());

		} else if (type == "separator") {
			QTreeWidgetItem *it = makeSeparatorItem(parent);
			QString cs = obj["color"].toString();
			if (!cs.isEmpty())
				it->setData(0, RoleColor, QColor(cs));

		} else if (type == "textfield") {
			QTreeWidgetItem *it = makeTextFieldItem(
				parent, obj["text"].toString());
			QString cs = obj["color"].toString();
			if (!cs.isEmpty())
				applyColor(it, QColor(cs));

		} else if (type == "scene") {
			QTreeWidgetItem *it =
				makeSceneItem(parent, obj["name"].toString());
			QString cs = obj["color"].toString();
			if (!cs.isEmpty())
				applyColor(it, QColor(cs));
		}
	}
}

/* ================================================================== */
/* Registration                                                        */
/* ================================================================== */

static void scene_organiser_cleanup_dock(bool frontendAvailable)
{
	if (!g_dock)
		return;

	g_dock->PrepareShutdown(frontendAvailable);

	if (frontendAvailable && g_dock_registered) {
		obs_frontend_remove_dock(DOCK_ID);
		g_dock_registered = false;
	} else if (!frontendAvailable) {
		g_dock_registered = false;
	}

	g_dock = nullptr;
}

extern "C" void scene_organiser_register_dock(void)
{
	if (g_dock)
		return;

	g_frontend_api_closed = false;
	g_dock = new SceneOrganiserDock();
	if (!obs_frontend_add_dock_by_id(DOCK_ID,
					 obs_module_text("Organiser.Dock.Title"),
					 g_dock)) {
		obs_log(LOG_WARNING, "dock registration failed: %s", DOCK_ID);
		delete g_dock;
		g_dock = nullptr;
		return;
	}

	g_dock_registered = true;
	g_dock->RegisterFrontendCallback();
	obs_log(LOG_INFO, "dock registered: %s", DOCK_ID);
}

extern "C" void scene_organiser_unregister_dock(void)
{
	scene_organiser_cleanup_dock(!g_frontend_api_closed);
}
