#include <ttkMacros.h>
#include <ttkMorseSmaleQuadrangulation.h>
#include <ttkUtils.h>

#include <vtkInformation.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

vtkStandardNewMacro(ttkMorseSmaleQuadrangulation);

ttkMorseSmaleQuadrangulation::ttkMorseSmaleQuadrangulation() {
  // critical points + 1-separatrices + segmentation
  SetNumberOfInputPorts(3);
  // quad mesh (containing ttkVertexIdentifiers of critical points)
  SetNumberOfOutputPorts(1);
}

int ttkMorseSmaleQuadrangulation::FillInputPortInformation(
  int port, vtkInformation *info) {
  if(port == 0 || port == 1) {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkUnstructuredGrid");
    return 1;
  }
  return 0;
}

int ttkMorseSmaleQuadrangulation::FillOutputPortInformation(
  int port, vtkInformation *info) {
  if(port == 0) {
    info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkUnstructuredGrid");
    return 1;
  }
  return 0;
}

int ttkMorseSmaleQuadrangulation::RequestData(
  vtkInformation *request,
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector) {

  auto critpoints = vtkUnstructuredGrid::GetData(inputVector[0]);
  auto seprs = vtkUnstructuredGrid::GetData(inputVector[1]);
  auto domain = vtkUnstructuredGrid::GetData(inputVector[2]);
  auto output = vtkUnstructuredGrid::GetData(outputVector);

  auto triangulation = ttkAlgorithm::GetTriangulation(domain);
  if(triangulation == nullptr) {
    return 0;
  }
  this->preconditionTriangulation(triangulation);

  auto cpPoints = critpoints->GetPoints();
  auto cpData = critpoints->GetPointData();
  auto domainPoints = domain->GetPoints();
  auto seprsPoints = seprs->GetPoints();
  auto seprsData = seprs->GetPointData();

  if(domainPoints == nullptr || seprsPoints == nullptr || seprsData == nullptr
     || cpPoints == nullptr || cpData == nullptr) {
    this->printErr("Invalid input");
    return 0;
  }
  this->setInputPoints(ttkUtils::GetVoidPointer(domainPoints));

  auto cpci = cpData->GetArray("CellId");
  auto cpcd = cpData->GetArray("CellDimension");
  auto cpid = cpData->GetArray(ttk::VertexScalarFieldName);
  auto sepid = seprsData->GetArray("CellId");
  auto sepdim = seprsData->GetArray("CellDimension");
  auto sepmask = seprsData->GetArray(ttk::MaskScalarFieldName);

  if(cpci == nullptr || cpcd == nullptr || cpid == nullptr || sepid == nullptr
     || sepdim == nullptr || sepmask == nullptr) {
    this->printErr("Missing data arrays");
    return 0;
  }

  this->setCriticalPoints(
    cpPoints->GetNumberOfPoints(), ttkUtils::GetVoidPointer(cpPoints),
    ttkUtils::GetVoidPointer(cpid), ttkUtils::GetVoidPointer(cpci),
    ttkUtils::GetVoidPointer(cpcd));

  this->setSeparatrices(
    sepid->GetNumberOfTuples(), ttkUtils::GetVoidPointer(sepid),
    ttkUtils::GetVoidPointer(sepdim), ttkUtils::GetVoidPointer(sepmask),
    ttkUtils::GetVoidPointer(seprsPoints));

  int res{-1};
  res = this->execute();

  if(res != 0) {
    this->printWrn("Consider another (eigen) function, persistence threshold "
                   "or refine your input triangulation");
    if(!ShowResError) {
      return 0;
    }
  }

  // output points: critical points + generated separatrices middles
  auto outQuadPoints = vtkSmartPointer<vtkPoints>::New();
  for(size_t i = 0; i < outputPoints_.size() / 3; i++) {
    outQuadPoints->InsertNextPoint(&outputPoints_[3 * i]);
  }
  output->SetPoints(outQuadPoints);

  // quad vertices identifiers
  auto identifiers = vtkSmartPointer<ttkSimplexIdTypeArray>::New();
  identifiers->SetName(ttk::VertexScalarFieldName);
  ttkUtils::SetVoidArray(
    identifiers, outputPointsIds_.data(), outputPointsIds_.size(), 1);
  output->GetPointData()->AddArray(identifiers);

  // quad vertices type
  auto type = vtkSmartPointer<ttkSimplexIdTypeArray>::New();
  type->SetName("QuadVertType");
  ttkUtils::SetVoidArray(
    type, outputPointsTypes_.data(), outputPointsTypes_.size(), 1);
  output->GetPointData()->AddArray(type);

  // quad vertices cells
  auto cellid = vtkSmartPointer<ttkSimplexIdTypeArray>::New();
  cellid->SetName("QuadCellId");
  ttkUtils::SetVoidArray(
    cellid, outputPointsCells_.data(), outputPointsCells_.size(), 1);
  output->GetPointData()->AddArray(cellid);

  // vtkCellArray of quadrangle values containing outArray
  auto cells = vtkSmartPointer<vtkCellArray>::New();
  for(size_t i = 0; i < outputCells_.size() / 5; i++) {
    cells->InsertNextCell(4, &outputCells_[5 * i + 1]);
  }

  // update output: get quadrangle values
  output->SetCells(VTK_QUAD, cells);

  // shallow copy input field data
  output->GetFieldData()->ShallowCopy(domain->GetFieldData());

  return 1;
}
