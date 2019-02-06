#pragma once

#include <QList>
#include <QVector>
#include <QPointer>
#include <QListView>
#include <QCheckBox>
#include <QAbstractListModel>
#include "volume-control.hpp"

class QLabel;
class QCheckBox;
class QLineEdit;
class MixerTree;
class QSpacerItem;
class QHBoxLayout;
class LockedCheckBox;
class VisibilityCheckBox;
class VisibilityItemWidget;

typedef VolControl MixerTreeItem;

class MixerTreeSubItemCheckBox : public QCheckBox {
	Q_OBJECT
};

class MixerTreeModel : public QAbstractListModel {
	Q_OBJECT

	friend class MixerTree;
	friend class VolControl;

	MixerTree *st;
	QVector<OBSSceneItem> items;
	bool hasGroups = false;

	static void OBSFrontendEvent(enum obs_frontend_event event, void *ptr);
	void Clear();
	void SceneChanged();
	void ReorderItems();

	void Add(obs_sceneitem_t *item);
	void Remove(obs_sceneitem_t *item);
	OBSSceneItem Get(int idx);

public:
	explicit MixerTreeModel(MixerTree *st);
	~MixerTreeModel();

	virtual int rowCount(const QModelIndex &parent) const override;
	virtual QVariant data(const QModelIndex &index, int role) const override;

	virtual Qt::ItemFlags flags(const QModelIndex &index) const override;
	virtual Qt::DropActions supportedDropActions() const override;
};

class MixerTree : public QListView {
	Q_OBJECT

	bool ignoreReorder = false;

	friend class MixerTreeModel;
	friend class VolControl;

	void ResetWidgets();
	void UpdateWidget(const QModelIndex &idx, obs_sceneitem_t *item);
	void UpdateWidgets(bool force = false);

	inline MixerTreeModel *GetStm() const
	{
		return reinterpret_cast<MixerTreeModel *>(model());
	}

public:
	inline MixerTreeItem *GetItemWidget(int idx)
	{
		QWidget *widget = indexWidget(GetStm()->createIndex(idx, 0));
		return reinterpret_cast<MixerTreeItem *>(widget);
	}

	explicit MixerTree(QWidget *parent = nullptr);

	inline bool IgnoreReorder() const {return ignoreReorder;}
	inline void Clear() {GetStm()->Clear();}

	inline void Add(obs_sceneitem_t *item) {GetStm()->Add(item);}
	inline OBSSceneItem Get(int idx) {return GetStm()->Get(idx);}

	void SelectItem(obs_sceneitem_t *sceneitem, bool select);

	bool MultipleBaseSelected() const;
	/*
	QSize sizeHintForIndex(const QModelIndex &index) const;
	*/
public slots:
	inline void ReorderItems() {GetStm()->ReorderItems();}
	void Remove(OBSSceneItem item);
	void Edit(int idx);

protected:
	virtual void mouseDoubleClickEvent(QMouseEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;

	virtual void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
};
