/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Kitware Inc.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Julien Finet, Kitware Inc.
  and was partially funded by NIH grant 3P41RR013218-12S1

==============================================================================*/

// Qt includes
#include <QDebug>
#include <QStyle>

#include <QColor>

// Slicer includes
#include "qSlicerApplication.h"
#include "qMRMLPlannerModelHierarchyModel.h"

// MRML includes
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLDisplayableHierarchyNode.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLTransformDisplayNode.h>
#include <vtkMRMLTransformNode.h>
#include <vtkMRMLTransformableNode.h>

//------------------------------------------------------------------------------
class qMRMLPlannerModelHierarchyModelPrivate
{
public:
  qMRMLPlannerModelHierarchyModelPrivate();

  bool hasTransformableNodeChildren(vtkMRMLNode* node) const;

  vtkMRMLTransformDisplayNode* transformDisplayNode(
    vtkMRMLScene* scene,vtkMRMLNode* node) const;

  int TransformVisibilityColumn;
};

//------------------------------------------------------------------------------
qMRMLPlannerModelHierarchyModelPrivate::qMRMLPlannerModelHierarchyModelPrivate()
{
  this->TransformVisibilityColumn = -1;
}

//------------------------------------------------------------------------------
vtkMRMLTransformDisplayNode* qMRMLPlannerModelHierarchyModelPrivate
::transformDisplayNode(vtkMRMLScene* scene, vtkMRMLNode* node) const
{
  if (!node)
    {
    return NULL;
    }

  return vtkMRMLTransformDisplayNode::SafeDownCast(node->GetNodeReference(
    qMRMLPlannerModelHierarchyModel::transformDisplayReferenceRole()));
}

//------------------------------------------------------------------------------
bool qMRMLPlannerModelHierarchyModelPrivate
::hasTransformableNodeChildren(vtkMRMLNode* node) const
{
  vtkMRMLDisplayableHierarchyNode* displayableHierarchyNode
    = vtkMRMLDisplayableHierarchyNode::SafeDownCast(node);
  if (displayableHierarchyNode)
    {
    std::vector<vtkMRMLHierarchyNode*> children;
    displayableHierarchyNode->GetAllChildrenNodes(children);
    std::vector<vtkMRMLHierarchyNode*>::const_iterator it;
    for (it = children.begin(); it != children.end(); ++it)
      {
      if (vtkMRMLTransformableNode::SafeDownCast((*it)->GetAssociatedNode()))
        {
        return true;
        }
      }
    }
  return false;
}

//----------------------------------------------------------------------------
//------------------------------------------------------------------------------
qMRMLPlannerModelHierarchyModel::qMRMLPlannerModelHierarchyModel(QObject *vparent)
  : Superclass(vparent)
  , d_ptr( new qMRMLPlannerModelHierarchyModelPrivate )
{
}

//------------------------------------------------------------------------------
qMRMLPlannerModelHierarchyModel::~qMRMLPlannerModelHierarchyModel()
{
}

//------------------------------------------------------------------------------
const char* qMRMLPlannerModelHierarchyModel::transformDisplayReferenceRole()
{
  return "Planner/TransformDisplayID";
}

//------------------------------------------------------------------------------
void qMRMLPlannerModelHierarchyModel::observeNode(vtkMRMLNode* node)
{
  this->Superclass::observeNode(node);
  if (node->IsA("vtkMRMLModelHierarchyNode") || node->IsA("vtkMRMLModelNode"))
    {
    qvtkConnect(node, vtkMRMLNode::ReferenceAddedEvent,
                this, SLOT(onReferenceChangedEvent(vtkObject*)));
    qvtkConnect(node, vtkMRMLNode::ReferenceModifiedEvent,
                this, SLOT(onReferenceChangedEvent(vtkObject*)));
    qvtkConnect(node, vtkMRMLNode::ReferenceRemovedEvent,
                this, SLOT(onReferenceChangedEvent(vtkObject*)));
    }
}

//------------------------------------------------------------------------------
void qMRMLPlannerModelHierarchyModel::onReferenceChangedEvent(vtkObject* object)
{
  Q_D(const qMRMLPlannerModelHierarchyModel);
  vtkMRMLNode* node = vtkMRMLNode::SafeDownCast(object);

  vtkMRMLTransformDisplayNode* display =
    d->transformDisplayNode(this->mrmlScene(), vtkMRMLNode::SafeDownCast(object));
  qvtkConnect(display, vtkCommand::ModifiedEvent, this, SLOT(modifyNode(vtkObject*)));
}

//------------------------------------------------------------------------------
void qMRMLPlannerModelHierarchyModel::modifyNode(vtkObject* object)
{
  this->updateNodeItems();
}

//------------------------------------------------------------------------------
QFlags<Qt::ItemFlag> qMRMLPlannerModelHierarchyModel
::nodeFlags(vtkMRMLNode* node, int column)const
{
  Q_D(const qMRMLPlannerModelHierarchyModel);
  QFlags<Qt::ItemFlag> flags = this->Superclass::nodeFlags(node, column);

  vtkMRMLTransformableNode* transformable = vtkMRMLTransformableNode::SafeDownCast(node);
  if (column == this->transformVisibilityColumn() &&
    (transformable || d->hasTransformableNodeChildren(node)))
    {
    flags |= Qt::ItemIsUserCheckable;
    }
  return flags;
}

//------------------------------------------------------------------------------
void qMRMLPlannerModelHierarchyModel
::updateItemDataFromNode(QStandardItem* item, vtkMRMLNode* node, int column)
{
  Q_D(qMRMLPlannerModelHierarchyModel);
  if (column == this->transformVisibilityColumn())
    {
    vtkMRMLTransformDisplayNode* display =
      d->transformDisplayNode(this->mrmlScene(), node);
    if (display)
      {
      item->setToolTip("Save node");
      item->setCheckState(
        display->GetEditorVisibility() ? Qt::Checked : Qt::Unchecked);
      }
    }
  this->Superclass::updateItemDataFromNode(item, node, column);
}

//------------------------------------------------------------------------------
void qMRMLPlannerModelHierarchyModel
::updateNodeFromItemData(vtkMRMLNode* node, QStandardItem* item)
{
  Q_D(qMRMLPlannerModelHierarchyModel);
  if (item->column() == this->transformVisibilityColumn())
    {
    vtkMRMLTransformDisplayNode* display =
      d->transformDisplayNode(this->mrmlScene(), node);
    if (display)
      {
      display->SetEditorVisibility(
        item->checkState() == Qt::Checked ? true : false);
      display->UpdateEditorBounds();
      }
    }
  return this->Superclass::updateNodeFromItemData(node, item);
}

//------------------------------------------------------------------------------
int qMRMLPlannerModelHierarchyModel::transformVisibilityColumn()const
{
  Q_D(const qMRMLPlannerModelHierarchyModel);
  return d->TransformVisibilityColumn;
}

//------------------------------------------------------------------------------
void qMRMLPlannerModelHierarchyModel::setTransformVisibilityColumn(int column)
{
  Q_D(qMRMLPlannerModelHierarchyModel);
  d->TransformVisibilityColumn = column;
  this->updateColumnCount();
}

//------------------------------------------------------------------------------
int qMRMLPlannerModelHierarchyModel::maxColumnId()const
{
  Q_D(const qMRMLPlannerModelHierarchyModel);
  int maxId = this->Superclass::maxColumnId();
  maxId = qMax(maxId, d->TransformVisibilityColumn);
  return maxId;
}
