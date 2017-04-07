/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// Qt includes
#include <QDebug>
#include <QMessageBox>
#include <QSettings>

// CTK includes
#include "ctkMessageBox.h"

// VTK includes
#include "vtkNew.h"

// SlicerQt includes
#include "qSlicerApplication.h"
#include "qSlicerIOManager.h"
#include "qSlicerFileDialog.h"
#include "qMRMLSceneModelHierarchyModel.h"
#include "qSlicerPlannerModuleWidget.h"
#include "ui_qSlicerPlannerModuleWidget.h"

// Slicer
#include "vtkMRMLDisplayableHierarchyLogic.h"
#include "vtkMRMLLinearTransformNode.h"
#include "vtkMRMLModelHierarchyNode.h"
#include "vtkMRMLModelNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLTransformDisplayNode.h"

// Self
#include "qMRMLPlannerModelHierarchyModel.h"
#include "vtkSlicerPlannerLogic.h"

//-----------------------------------------------------------------------------
/// \ingroup Slicer_QtModules_ExtensionTemplate
class qSlicerPlannerModuleWidgetPrivate: public Ui_qSlicerPlannerModuleWidget
{
public:
  qSlicerPlannerModuleWidgetPrivate();
  void fireDeleteChildrenWarning() const;

  void createTransformsIfNecessary(
    vtkMRMLScene* scene, vtkMRMLModelHierarchyNode* refNode);
  vtkMRMLLinearTransformNode* createTransformNode(
    vtkMRMLScene* scene, vtkMRMLNode* refNode);
  vtkMRMLLinearTransformNode* getTransformNode(
    vtkMRMLScene* scene, vtkMRMLNode* refNode) const;
  void removeTransformNode(vtkMRMLScene* scene, vtkMRMLNode* nodeRef);

  qMRMLPlannerModelHierarchyModel* sceneModel() const;
  void updateDisplayTransformNode(vtkMRMLNode* nodeRef) const;

  void updateWidgetFromReferenceNode(
    vtkMRMLNode* node,
    ctkColorPickerButton* button,
    qMRMLSliderWidget* slider) const;
  void updateReferenceNodeFromWidget(
    vtkMRMLNode* node, QColor color, double opacity) const;
  vtkMRMLNode* openReferenceDialog() const;

  vtkMRMLModelHierarchyNode* HierarchyNode;
  vtkMRMLModelHierarchyNode* StagedHierarchyNode;
  QStringList HideChildNodeTypes;
  QMap<QString, QString> Transforms;
  vtkMRMLNode* BrainReferenceNode;
  vtkMRMLNode* TemplateReferenceNode;
};

//-----------------------------------------------------------------------------
// qSlicerPlannerModuleWidgetPrivate methods

//-----------------------------------------------------------------------------
qSlicerPlannerModuleWidgetPrivate::qSlicerPlannerModuleWidgetPrivate()
{
  this->HierarchyNode = NULL;
  this->StagedHierarchyNode = NULL;
  this->HideChildNodeTypes =
    (QStringList() << "vtkMRMLFiberBundleNode" << "vtkMRMLAnnotationNode");
  this->BrainReferenceNode = NULL;
  this->TemplateReferenceNode = NULL;
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidgetPrivate::fireDeleteChildrenWarning() const
{
  ctkMessageBox deleteChildrenDialog;
  deleteChildrenDialog.setText(
    "Deleting this model hierarchy will also delete all the model children.");
  deleteChildrenDialog.setWindowTitle("Warning");
  deleteChildrenDialog.setIcon(QMessageBox::Warning);
  deleteChildrenDialog.setStandardButtons(QMessageBox::Ok);
  deleteChildrenDialog.setDontShowAgainSettingsKey(
    vtkSlicerPlannerLogic::DeleteChildrenWarningSettingName());
  deleteChildrenDialog.exec();
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidgetPrivate
::createTransformsIfNecessary(vtkMRMLScene* scene, vtkMRMLModelHierarchyNode* hierarchy)
{
  if (!hierarchy)
    {
    return;
    }

  vtkMRMLLinearTransformNode* transform = this->getTransformNode(scene, hierarchy);
  if (!transform)
    {
    transform = this->createTransformNode(scene, hierarchy);
    }

  std::vector<vtkMRMLHierarchyNode*> children;
  std::vector<vtkMRMLHierarchyNode*>::const_iterator it;
  hierarchy->GetAllChildrenNodes(children);
  for (it = children.begin(); it != children.end(); ++it)
    {
    vtkMRMLModelNode* childModel =
      vtkMRMLModelNode::SafeDownCast((*it)->GetAssociatedNode());
    if (childModel)
      {
      vtkMRMLLinearTransformNode* childTransform = this->getTransformNode(scene, childModel);
      if (!childTransform)
        {
        childTransform = this->createTransformNode(scene, childModel);
        childModel->SetAndObserveTransformNodeID(childTransform->GetID());
        }
      childTransform->SetAndObserveTransformNodeID(transform->GetID());
      }
    }
}

//-----------------------------------------------------------------------------
vtkMRMLLinearTransformNode* qSlicerPlannerModuleWidgetPrivate
::createTransformNode(vtkMRMLScene* scene, vtkMRMLNode* refNode)
{
  Q_ASSERT(scene);
  vtkNew<vtkMRMLLinearTransformNode> newTransform;
  vtkMRMLLinearTransformNode* transform =
    vtkMRMLLinearTransformNode::SafeDownCast(
      scene->AddNode(newTransform.GetPointer()));
  QString transformName = refNode->GetName();
  transformName += "_Transform";
  transform->SetName(transformName.toLatin1());

  vtkNew<vtkMRMLTransformDisplayNode> newDisplay;
  vtkMRMLNode* display = scene->AddNode(newDisplay.GetPointer());
  transform->SetAndObserveDisplayNodeID(display->GetID());

  this->Transforms[refNode->GetID()] = transform->GetID();
  refNode->SetNodeReferenceID(
    this->sceneModel()->transformDisplayReferenceRole(), display->GetID());
  return transform;
}

//-----------------------------------------------------------------------------
vtkMRMLLinearTransformNode* qSlicerPlannerModuleWidgetPrivate
::getTransformNode(vtkMRMLScene* scene, vtkMRMLNode* refNode) const
{
  if (refNode && this->Transforms.contains(refNode->GetID()))
    {
    return vtkMRMLLinearTransformNode::SafeDownCast(
      scene->GetNodeByID(this->Transforms[refNode->GetID()].toAscii()));
    }
  return NULL;
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidgetPrivate
::removeTransformNode(vtkMRMLScene* scene, vtkMRMLNode* nodeRef)
{
  vtkMRMLLinearTransformNode* transform = this->getTransformNode(scene, nodeRef);
  if (transform)
    {
    this->Transforms.remove(nodeRef->GetID());
    scene->RemoveNode(transform);
    }
}

//-----------------------------------------------------------------------------
qMRMLPlannerModelHierarchyModel* qSlicerPlannerModuleWidgetPrivate
::sceneModel() const
{
  return qobject_cast<qMRMLPlannerModelHierarchyModel*>(
    this->ModelHierarchyTreeView->sceneModel());
}


//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidgetPrivate::updateWidgetFromReferenceNode(
  vtkMRMLNode* node,
  ctkColorPickerButton* button,
  qMRMLSliderWidget* slider) const
{
  vtkMRMLModelNode* model = vtkMRMLModelNode::SafeDownCast(node);
  button->setEnabled(model != NULL);
  slider->setEnabled(model != NULL);
  if (model)
    {
    vtkMRMLDisplayNode* display = model->GetDisplayNode();
    if (display)
      {
      double rgb[3];
      display->GetColor(rgb);

      bool wasBlocking = button->blockSignals(true);
      button->setColor(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]));
      button->blockSignals(wasBlocking);

      wasBlocking = slider->blockSignals(true);
      slider->setValue(display->GetOpacity());
      slider->blockSignals(wasBlocking);
      }
    }
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidgetPrivate::updateReferenceNodeFromWidget(
  vtkMRMLNode* node, QColor color, double opacity) const
{
  vtkMRMLModelNode* model = vtkMRMLModelNode::SafeDownCast(node);
  if (model)
    {
    vtkMRMLDisplayNode* display = model->GetDisplayNode();
    if (display)
      {
      int wasModifying = display->StartModify();
      display->SetColor(color.redF(), color.greenF(), color.blueF());
      display->SetOpacity(opacity);
      display->EndModify(wasModifying);
      }
    }
}

//-----------------------------------------------------------------------------
vtkMRMLNode* qSlicerPlannerModuleWidgetPrivate::openReferenceDialog() const
{
  qSlicerIOManager* ioManager = qSlicerApplication::application()->ioManager();
  vtkNew<vtkCollection> loadedNodes;
  bool success = ioManager->openDialog(
    "ModelFile", qSlicerFileDialog::Read,
    qSlicerIO::IOProperties(), loadedNodes.GetPointer());
  if (success && loadedNodes->GetNumberOfItems() > 0)
    {
    return vtkMRMLNode::SafeDownCast(loadedNodes->GetItemAsObject(0));
    }
  return NULL;
}

//-----------------------------------------------------------------------------
// qSlicerPlannerModuleWidget methods

//-----------------------------------------------------------------------------
qSlicerPlannerModuleWidget::qSlicerPlannerModuleWidget(QWidget* _parent)
  : Superclass( _parent )
  , d_ptr( new qSlicerPlannerModuleWidgetPrivate )
{
}

//-----------------------------------------------------------------------------
qSlicerPlannerModuleWidget::~qSlicerPlannerModuleWidget()
{
  Q_D(qSlicerPlannerModuleWidget);
}

//-----------------------------------------------------------------------------
vtkSlicerPlannerLogic* qSlicerPlannerModuleWidget::plannerLogic() const
{
  return vtkSlicerPlannerLogic::SafeDownCast(this->logic());
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::setup()
{
  Q_D(qSlicerPlannerModuleWidget);
  d->setupUi(this);
  this->Superclass::setup();

  qMRMLPlannerModelHierarchyModel* sceneModel =
    new qMRMLPlannerModelHierarchyModel(this);

  d->ModelHierarchyTreeView->setSceneModel(sceneModel, "Planner");
  d->ModelHierarchyTreeView->setSceneModelType("Planner");
  d->ModelHierarchyTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
  sceneModel->setIDColumn(-1);
  sceneModel->setHeaderData(0, Qt::Horizontal, "Node");
  sceneModel->setExpandColumn(1);
  sceneModel->setHeaderData(1, Qt::Horizontal, ""); // Don't know a good descriptor
  sceneModel->setColorColumn(2);
  sceneModel->setHeaderData(2, Qt::Horizontal, "Color");
  sceneModel->setOpacityColumn(3);
  sceneModel->setHeaderData(3, Qt::Horizontal, "Opacity");
  sceneModel->setTransformVisibilityColumn(4);
  sceneModel->setHeaderData(4, Qt::Horizontal, "Transform");
  // use lazy update instead of responding to scene import end event
  sceneModel->setLazyUpdate(true);

  d->ModelHierarchyTreeView->setHeaderHidden(false);
  d->ModelHierarchyTreeView->header()->setStretchLastSection(false);
  d->ModelHierarchyTreeView->header()->setResizeMode(sceneModel->nameColumn(), QHeaderView::Stretch);
  d->ModelHierarchyTreeView->header()->setResizeMode(sceneModel->expandColumn(), QHeaderView::ResizeToContents);
  d->ModelHierarchyTreeView->header()->setResizeMode(sceneModel->colorColumn(), QHeaderView::ResizeToContents);
  d->ModelHierarchyTreeView->header()->setResizeMode(sceneModel->opacityColumn(), QHeaderView::ResizeToContents);
  d->ModelHierarchyTreeView->header()->setResizeMode(sceneModel->transformVisibilityColumn(), QHeaderView::ResizeToContents);

  d->ModelHierarchyTreeView->sortFilterProxyModel()->setHideChildNodeTypes(d->HideChildNodeTypes);
  d->ModelHierarchyTreeView->sortFilterProxyModel()->invalidate();

  QIcon loadIcon =
    qSlicerApplication::application()->style()->standardIcon(QStyle::SP_DialogOpenButton);
  d->BrainReferenceOpenButton->setIcon(loadIcon);
  d->TemplateReferenceOpenButton->setIcon(loadIcon);

  // Connect
  this->connect(
    d->ModelHierarchyNodeComboBox, SIGNAL(currentNodeChanged(vtkMRMLNode*)),
    this, SLOT(setCurrentNode(vtkMRMLNode*)));
  this->connect(
    d->ModelHierarchyNodeComboBox, SIGNAL(nodeAboutToBeRemoved(vtkMRMLNode*)),
    this, SLOT(onNodeAboutToBeRemoved(vtkMRMLNode*)));

  this->connect(
    d->BrainReferenceNodeComboBox, SIGNAL(currentNodeChanged(vtkMRMLNode*)),
    this, SLOT(updateBrainReferenceNode(vtkMRMLNode*)));
  this->connect(
    d->TemplateReferenceNodeComboBox, SIGNAL(currentNodeChanged(vtkMRMLNode*)),
    this, SLOT(updateTemplateReferenceNode(vtkMRMLNode*)));

  this->connect(
    d->BrainReferenceColorPickerButton, SIGNAL(colorChanged(QColor)),
    this, SLOT(updateMRMLFromWidget()));
  this->connect(
    d->TemplateReferenceColorPickerButton, SIGNAL(colorChanged(QColor)),
    this, SLOT(updateMRMLFromWidget()));
  this->connect(
    d->BrainReferenceOpacitySliderWidget, SIGNAL(valueChanged(double)),
    this, SLOT(updateMRMLFromWidget()));
  this->connect(
    d->TemplateReferenceOpacitySliderWidget, SIGNAL(valueChanged(double)),
    this, SLOT(updateMRMLFromWidget()));
  this->connect(
    d->BrainReferenceOpenButton, SIGNAL(clicked()),
    this, SLOT(onOpenBrainReference()));
  this->connect(
    d->TemplateReferenceOpenButton, SIGNAL(clicked()),
    this, SLOT(onOpenTemplateReference()));
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::setCurrentNode(vtkMRMLNode* node)
{
  Q_D(qSlicerPlannerModuleWidget);
  vtkMRMLModelHierarchyNode* hNode = vtkMRMLModelHierarchyNode::SafeDownCast(node);
  d->HierarchyNode = hNode;
  this->updateWidgetFromMRML();
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::setMRMLScene(vtkMRMLScene* scene)
{
  Superclass::setMRMLScene(scene);

  this->qvtkReconnect(
    this->mrmlScene(), vtkMRMLScene::NodeAddedEvent,
    this, SLOT(onNodeAddedEvent(vtkObject*, vtkObject*)));

  this->qvtkReconnect(
    this->mrmlScene(), vtkMRMLScene::NodeAboutToBeRemovedEvent,
    this, SLOT(onNodeAboutToBeRemovedEvent(vtkObject*, vtkObject*)));
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget
::onNodeAddedEvent(vtkObject* scene, vtkObject* node)
{
  Q_D(qSlicerPlannerModuleWidget);
  Q_UNUSED(scene);
  vtkMRMLModelHierarchyNode* hNode =
    vtkMRMLModelHierarchyNode::SafeDownCast(node);
  if (!hNode || hNode->GetHideFromEditors())
    {
    return;
    }

  // OnNodeAddedEvent is here to make sure that the combobox is populated
  // too after a node is added to the scene, because the tree view will be
  // and they need to match.
  if (!d->HierarchyNode)
    {
    if (this->mrmlScene()->IsBatchProcessing())
      {
      // Problem is, during a batch processing, the model is yet up-to-date.
      // So we wait for the sceneUpdated() signal and then do the update.
      d->StagedHierarchyNode = hNode;
      this->connect(
        d->ModelHierarchyTreeView->sceneModel(), SIGNAL(sceneUpdated()),
        this, SLOT(onSceneUpdated()));
      }
    else
      {
      // No problem, just do the update directly.
      this->setCurrentNode(hNode);
      }
    }
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget
::onNodeAboutToBeRemovedEvent(vtkObject* sceneObject, vtkObject* nodeObject)
{
  Q_D(qSlicerPlannerModuleWidget);
  vtkMRMLScene* scene = vtkMRMLScene::SafeDownCast(sceneObject);
  vtkMRMLNode* node = vtkMRMLNode::SafeDownCast(nodeObject);

  vtkMRMLLinearTransformNode* transform =
    vtkMRMLLinearTransformNode::SafeDownCast(node);
  if (transform)
    {
    d->Transforms.remove(d->Transforms.key(transform->GetID()));

    if (!this->mrmlScene()->IsClosing())
      {
      this->updateWidgetFromMRML();
      }
    }
  d->removeTransformNode(scene, node);
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::onSceneUpdated()
{
  Q_D(qSlicerPlannerModuleWidget);
  this->disconnect(this, SLOT(onSceneUpdated()));
  if (!d->HierarchyNode && d->StagedHierarchyNode != d->HierarchyNode)
    {
    this->setCurrentNode(d->StagedHierarchyNode);
    d->StagedHierarchyNode = NULL;
    }
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::onNodeAboutToBeRemoved(vtkMRMLNode* node)
{
  Q_D(qSlicerPlannerModuleWidget);
  vtkMRMLModelHierarchyNode* hierarchy =
    vtkMRMLModelHierarchyNode::SafeDownCast(node);
  if (hierarchy && hierarchy == d->HierarchyNode)
    {
    d->fireDeleteChildrenWarning();
    this->plannerLogic()->DeleteHierarchyChildren(hierarchy);
    }
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget
::updateWidgetFromMRML(vtkObject* obj1, vtkObject* obj2)
{
  Q_UNUSED(obj1);
  Q_UNUSED(obj2);
  this->updateWidgetFromMRML();
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::updateWidgetFromMRML()
{
  Q_D(qSlicerPlannerModuleWidget);
  // Inputs
  d->ModelHierarchyNodeComboBox->setCurrentNode(d->HierarchyNode);

  d->ModelHierarchyTreeView->setEnabled(d->HierarchyNode != NULL);
  d->ModelHierarchyTreeView->setRootNode(d->HierarchyNode);
  d->ModelHierarchyTreeView->setCurrentNode(d->HierarchyNode);
  d->ModelHierarchyTreeView->expandAll();

  // Create all the transforms for the current hierarchy node
  d->createTransformsIfNecessary(this->mrmlScene(), d->HierarchyNode);

  d->updateWidgetFromReferenceNode(
    d->BrainReferenceNodeComboBox->currentNode(),
    d->BrainReferenceColorPickerButton,
    d->BrainReferenceOpacitySliderWidget);
  d->updateWidgetFromReferenceNode(
    d->TemplateReferenceNodeComboBox->currentNode(),
    d->TemplateReferenceColorPickerButton,
    d->TemplateReferenceOpacitySliderWidget);
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::updateBrainReferenceNode(vtkMRMLNode* node)
{
  Q_D(qSlicerPlannerModuleWidget);
  this->qvtkReconnect(d->BrainReferenceNode, node,
    vtkCommand::ModifiedEvent,
    this, SLOT(updateWidgetFromMRML(vtkObject*, vtkObject*)));
  this->qvtkReconnect(d->BrainReferenceNode, node,
    vtkMRMLDisplayableNode::DisplayModifiedEvent,
    this, SLOT(updateWidgetFromMRML(vtkObject*, vtkObject*)));
  d->BrainReferenceNode = node;

  this->updateMRMLFromWidget();
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::updateTemplateReferenceNode(vtkMRMLNode* node)
{
  Q_D(qSlicerPlannerModuleWidget);
  this->qvtkReconnect(d->TemplateReferenceNode, node, vtkCommand::ModifiedEvent,
    this, SLOT(updateWidgetFromMRML(vtkObject*, vtkObject*)));
  this->qvtkReconnect(d->TemplateReferenceNode, node,
    vtkMRMLDisplayableNode::DisplayModifiedEvent,
    this, SLOT(updateWidgetFromMRML(vtkObject*, vtkObject*)));
  d->TemplateReferenceNode = node;

  this->updateMRMLFromWidget();
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::updateMRMLFromWidget()
{
  Q_D(qSlicerPlannerModuleWidget);

  d->updateReferenceNodeFromWidget(
    d->BrainReferenceNodeComboBox->currentNode(),
    d->BrainReferenceColorPickerButton->color(),
    d->BrainReferenceOpacitySliderWidget->value());
  d->updateReferenceNodeFromWidget(
    d->TemplateReferenceNodeComboBox->currentNode(),
    d->TemplateReferenceColorPickerButton->color(),
    d->TemplateReferenceOpacitySliderWidget->value());
}

//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::onOpenBrainReference()
{
  Q_D(qSlicerPlannerModuleWidget);
  vtkMRMLNode* model = d->openReferenceDialog();
  if (model)
    {
    d->BrainReferenceNodeComboBox->setCurrentNode(model);
    }
}
  
//-----------------------------------------------------------------------------
void qSlicerPlannerModuleWidget::onOpenTemplateReference()
{
  Q_D(qSlicerPlannerModuleWidget);
  vtkMRMLNode* model = d->openReferenceDialog();
  if (model)
    {
    d->TemplateReferenceNodeComboBox->setCurrentNode(model);
    }
}
