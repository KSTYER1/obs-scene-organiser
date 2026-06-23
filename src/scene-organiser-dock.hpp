/*
 * obs-scene-organiser
 * Copyright (C) 2026 K_STYER1
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. See LICENSE for details.
 */
#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QSet>
#include <QJsonArray>
#include <QString>

#include <obs-frontend-api.h>

typedef struct calldata calldata_t;

static const int OrgFolder    = QTreeWidgetItem::UserType + 1;
static const int OrgScene     = QTreeWidgetItem::UserType + 2;
static const int OrgSeparator = QTreeWidgetItem::UserType + 3;
static const int OrgTextField = QTreeWidgetItem::UserType + 4;
static const int RoleColor         = Qt::UserRole;
static const int RoleObsName       = Qt::UserRole + 1;
static const int RoleSearchBlink   = Qt::UserRole + 2;

/* ------------------------------------------------------------------ */
class OrgTree : public QTreeWidget {
	Q_OBJECT
signals:
	void itemDropped();

protected:
	void dropEvent(QDropEvent *e) override;
};

/* ------------------------------------------------------------------ */
class ColorBarDelegate : public QStyledItemDelegate {
	Q_OBJECT
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter *p, const QStyleOptionViewItem &opt,
		   const QModelIndex &idx) const override;
};

/* ------------------------------------------------------------------ */
class SceneOrganiserDock : public QWidget {
	Q_OBJECT
public:
	explicit SceneOrganiserDock(QWidget *parent = nullptr);
	~SceneOrganiserDock() override;

	static void frontendEvent(obs_frontend_event event, void *data);
	static void sourceRenameSignal(void *data, calldata_t *cd);
	void PrepareShutdown(bool fromObsExit);
	void RegisterFrontendCallback();

public slots:
	void syncScenes();
	void highlightCurrentScene();
	void onCollectionChanged();
	void onCollectionRenamed();
	void onFinishedLoading();

private slots:
	void onItemClicked(QTreeWidgetItem *item, int col);
	void onItemDoubleClicked(QTreeWidgetItem *item, int col);
	void onContextMenu(const QPoint &pos);
	void onSearchChanged(const QString &text);
	void onItemChanged(QTreeWidgetItem *item, int col);
	void onItemDropped();
	void advanceSearchBlink();

private:
	/* actions */
	void addItem();
	void addFolder();
	void addFolder(QTreeWidgetItem *parent);
	void addSeparator();
	void addTextField();
	void renameItem(QTreeWidgetItem *item);
	void deleteFolder(QTreeWidgetItem *item);
	void deleteSeparator(QTreeWidgetItem *item);
	void deleteTextField(QTreeWidgetItem *item);
	void deleteSelected();
	void moveSelected(int direction);
	void duplicateSeparator(QTreeWidgetItem *item);
	void duplicateTextField(QTreeWidgetItem *item);
	void pickColor(QTreeWidgetItem *item);
	void clearColor(QTreeWidgetItem *item);
	void switchToScene(QTreeWidgetItem *item);
	void duplicateScene(QTreeWidgetItem *item);
	void removeScene(QTreeWidgetItem *item);
	void openFilters(QTreeWidgetItem *item);
	void sortChildren(QTreeWidgetItem *parent, Qt::SortOrder order);

	/* tree helpers */
	void applyColor(QTreeWidgetItem *item, const QColor &color);
	void collectObsNames(QTreeWidgetItem *parent, QSet<QString> &out) const;
	void removeOrphans(QTreeWidgetItem *parent, const QSet<QString> &valid);
	void highlightItems(QTreeWidgetItem *parent, const QString &current);
	bool replaceObsName(QTreeWidgetItem *parent, const QString &prevName,
			    const QString &newName);
	void onSourceRenamed(const QString &prevName, const QString &newName);
	bool filterItems(QTreeWidgetItem *parent, const QString &text,
			 QSet<QString> *sourceMatches);
	bool sceneContainsSource(const QString &sceneName,
				 const QString &text) const;
	void startSearchBlink(const QSet<QString> &sceneNames);
	void clearSearchBlink();
	void setSearchBlinkVisible(QTreeWidgetItem *parent, bool visible);
	QTreeWidgetItem *makeSceneItem(QTreeWidgetItem *parent, const QString &name);
	QTreeWidgetItem *makeFolderItem(QTreeWidgetItem *parent, const QString &name);
	QTreeWidgetItem *makeSeparatorItem(QTreeWidgetItem *parent);
	QTreeWidgetItem *makeTextFieldItem(QTreeWidgetItem *parent, const QString &text);

	/* persistence */
	void save();
	bool load();
	QString configPath() const;
	QString legacyConfigPath() const;
	QString configPathForCollection(const QString &collection,
					bool legacy) const;
	QString currentSceneCollectionName() const;
	QJsonObject itemToJson(QTreeWidgetItem *item) const;
	void itemsFromJson(QTreeWidgetItem *parent, const QJsonArray &arr);

	OrgTree    *m_tree          = nullptr;
	QLineEdit  *m_search        = nullptr;
	QTimer     *m_searchBlinkTimer = nullptr;
	QSet<QString> m_searchBlinkSceneNames;
	int         m_searchBlinkTicks = 0;

	bool m_inhibit = false;
	bool m_loaded  = false;
	bool m_frontendCallbackRegistered = false;
	bool m_sourceRenameSignalRegistered = false;
	bool m_shutdownPrepared = false;
	QString m_lastConfigPath;
};
