/*
 * obs-scene-organiser
 * Copyright (C) 2026 Awet
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
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QSet>
#include <QJsonArray>

#include <obs-frontend-api.h>

static const int OrgFolder    = QTreeWidgetItem::UserType + 1;
static const int OrgScene     = QTreeWidgetItem::UserType + 2;
static const int OrgSeparator = QTreeWidgetItem::UserType + 3;
static const int OrgTextField = QTreeWidgetItem::UserType + 4;
static const int RoleColor         = Qt::UserRole;
static const int RoleObsName       = Qt::UserRole + 1;
static const int RoleFolderScenes  = Qt::UserRole + 2;
static const int RoleFolderSources = Qt::UserRole + 3;

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

public slots:
	void syncScenes();
	void highlightCurrentScene();
	void onCollectionChanged();
	void onFinishedLoading();
	void recomputeCounters();

private slots:
	void onItemClicked(QTreeWidgetItem *item, int col);
	void onItemDoubleClicked(QTreeWidgetItem *item, int col);
	void onContextMenu(const QPoint &pos);
	void onSearchChanged(const QString &text);
	void onItemChanged(QTreeWidgetItem *item, int col);
	void onItemDropped();
	void scheduleRecount();

private:
	/* actions */
	void addItem();
	void addFolder();
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
	void filterItems(QTreeWidgetItem *parent, const QString &text);
	QTreeWidgetItem *makeSceneItem(QTreeWidgetItem *parent, const QString &name);
	QTreeWidgetItem *makeFolderItem(QTreeWidgetItem *parent, const QString &name);
	QTreeWidgetItem *makeSeparatorItem(QTreeWidgetItem *parent);
	QTreeWidgetItem *makeTextFieldItem(QTreeWidgetItem *parent, const QString &text);

	/* persistence */
	void save();
	void load();
	QString configPath() const;
	QJsonObject itemToJson(QTreeWidgetItem *item) const;
	void itemsFromJson(QTreeWidgetItem *parent, const QJsonArray &arr);

	/* counters */
	void walkTree(QTreeWidgetItem *item, int *outScenes, int *outSources);
	void updateHeaderLabel();
	static void onSourceSignal(void *data, calldata_t *cd);

	OrgTree    *m_tree          = nullptr;
	QLineEdit  *m_search        = nullptr;
	QLabel     *m_counter       = nullptr;
	QTimer     *m_recountTimer  = nullptr;
	int         m_globalScenes  = 0;
	int         m_globalSources = 0;

	bool m_inhibit = false;
	bool m_loaded  = false;
};
