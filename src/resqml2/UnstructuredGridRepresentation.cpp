/*-----------------------------------------------------------------------
Licensed to the Apache Software Foundation (ASF) under one
or more contributor license agreements.  See the NOTICE file
distributed with this work for additional information
regarding copyright ownership.  The ASF licenses this file
to you under the Apache License, Version 2.0 (the
"License"; you may not use this file except in compliance
with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.  See the License for the
specific language governing permissions and limitations
under the License.
-----------------------------------------------------------------------*/
#include "UnstructuredGridRepresentation.h"

#include <stdexcept>

#include <hdf5.h>

#include "../eml2/AbstractHdfProxy.h"

#include "AbstractLocal3dCrs.h"

using namespace std;
using namespace gsoap_resqml2_0_1;
using namespace RESQML2_NS;

const char* UnstructuredGridRepresentation::XML_TAG = "UnstructuredGridRepresentation";

ULONG64 UnstructuredGridRepresentation::getXyzPointCountOfPatch(unsigned int patchIndex) const
{
	if (patchIndex >= getPatchCount()) {
		throw out_of_range("The index of the patch is not in the allowed range of patch.");
	}

	return getNodeCount();
}

ULONG64 const * UnstructuredGridRepresentation::getCumulativeFaceCountPerCell() const
{
	if (constantFaceCountPerCell != 0) {
		throw logic_error("This grid has a constant count of faces per cell. Please compute the cumulative count by your own.");
	}

	if (!cumulativeFaceCountPerCell) {
		throw logic_error("The geometry must have been loaded first.");
	}

	return cumulativeFaceCountPerCell.get();
}

void UnstructuredGridRepresentation::getFaceCountPerCell(ULONG64 * faceCountPerCell) const
{
	getCumulativeFaceCountPerCell(faceCountPerCell);
	const ULONG64 cellCount = getCellCount();
	ULONG64 buffer = faceCountPerCell[0];
	ULONG64 bufferCumCount = 0;
	for (ULONG64 cumulativeFaceCountPerCellIndex = 1; cumulativeFaceCountPerCellIndex < cellCount; ++cumulativeFaceCountPerCellIndex)
	{
		bufferCumCount = faceCountPerCell[cumulativeFaceCountPerCellIndex];
		faceCountPerCell[cumulativeFaceCountPerCellIndex] -= buffer;
		buffer = bufferCumCount;
	}
}

void UnstructuredGridRepresentation::getNodeCountPerFace(ULONG64 * nodeCountPerFace) const
{
	getCumulativeNodeCountPerFace(nodeCountPerFace);
	const ULONG64 faceCount = getFaceCount();
	ULONG64 buffer = nodeCountPerFace[0];
	ULONG64 bufferCumCount = 0;
	for (ULONG64 cumulativeNodeCountPerFaceIndex = 1; cumulativeNodeCountPerFaceIndex < faceCount; ++cumulativeNodeCountPerFaceIndex)
	{
		bufferCumCount = nodeCountPerFace[cumulativeNodeCountPerFaceIndex];
		nodeCountPerFace[cumulativeNodeCountPerFaceIndex] -= buffer;
		buffer = bufferCumCount;
	}
}

void UnstructuredGridRepresentation::setGeometry(unsigned char * cellFaceIsRightHanded, double * points, ULONG64 pointCount, EML2_NS::AbstractHdfProxy * proxy,
	ULONG64 * faceIndicesPerCell, ULONG64 * faceIndicesCumulativeCountPerCell,
	ULONG64 faceCount, ULONG64 * nodeIndicesPerFace, ULONG64 * nodeIndicesCumulativeCountPerFace,
	gsoap_resqml2_0_1::resqml20__CellShape cellShape, RESQML2_NS::AbstractLocal3dCrs * localCrs)
{
	if (cellFaceIsRightHanded == nullptr)
		throw invalid_argument("The cellFaceIsRightHanded information cannot be null.");
	if (points == nullptr)
		throw invalid_argument("The points of the ijk grid cannot be null.");
	if (faceIndicesPerCell == nullptr)
		throw invalid_argument("The definition of the face indices per cell is incomplete.");
	if (faceIndicesCumulativeCountPerCell == nullptr)
		throw invalid_argument("The definition of the face indices count per cell is incomplete.");
	if (nodeIndicesPerFace == nullptr)
		throw invalid_argument("The definition of the node indices per face is incomplete.");
	if (nodeIndicesCumulativeCountPerFace == nullptr)
		throw invalid_argument("The definition of the node indices count per face is incomplete.");

	if (proxy == nullptr) {
		proxy = getRepository()->getDefaultHdfProxy();
		if (proxy == nullptr) {
			throw std::invalid_argument("A (default) HDF Proxy must be provided.");
		}
	}

	setGeometryUsingExistingDatasets(getHdfGroup() + "/CellFaceIsRightHanded", getHdfGroup() + "/Points", pointCount, proxy,
		getHdfGroup() + "/FacesPerCell/" + ELEMENTS_DS_NAME, getHdfGroup() + "/FacesPerCell/" + CUMULATIVE_LENGTH_DS_NAME,
		faceCount, getHdfGroup() + "/NodesPerFace/" + ELEMENTS_DS_NAME, getHdfGroup() + "/NodesPerFace/" + CUMULATIVE_LENGTH_DS_NAME,
		cellShape, localCrs);

	const ULONG64 cellCount = getCellCount();

	// HDF Face Right handness
	unsigned long long faceCountTmp = faceIndicesCumulativeCountPerCell[cellCount - 1]; // For GCC : ULONG64 is not exactly an unsigned long long with GCC but an uint64_t {aka long unsigned int}
	proxy->writeArrayNd(getHdfGroup(), "CellFaceIsRightHanded", H5T_NATIVE_UCHAR, cellFaceIsRightHanded, &faceCountTmp, 1);

	// HDF Face indices
	proxy->writeItemizedListOfList(getHdfGroup(), "FacesPerCell", H5T_NATIVE_ULLONG, faceIndicesCumulativeCountPerCell, cellCount, H5T_NATIVE_ULLONG, faceIndicesPerCell, faceIndicesCumulativeCountPerCell[cellCount - 1]);

	// HDF Node indices
	proxy->writeItemizedListOfList(getHdfGroup(), "NodesPerFace", H5T_NATIVE_ULLONG, nodeIndicesCumulativeCountPerFace, faceCount, H5T_NATIVE_ULLONG, nodeIndicesPerFace, nodeIndicesCumulativeCountPerFace[faceCount - 1]);

	// HDF points
	hsize_t numValues[2];
	numValues[0] = pointCount;
	numValues[1] = 3; // 3 for X, Y and Z
	proxy->writeArrayNdOfDoubleValues(getHdfGroup(), "Points", points, numValues, 2);
}

void UnstructuredGridRepresentation::setConstantCellShapeGeometry(unsigned char * cellFaceIsRightHanded, double * points,
	ULONG64 pointCount, ULONG64 faceCount, RESQML2_NS::AbstractLocal3dCrs * localCrs, EML2_NS::AbstractHdfProxy* proxy,
	ULONG64 * faceIndicesPerCell, ULONG64 faceCountPerCell,
	ULONG64 * nodeIndicesPerFace, ULONG64 nodeCountPerFace)
{
	if (cellFaceIsRightHanded == nullptr) {
		throw invalid_argument("The cellFaceIsRightHanded information cannot be null.");
	}
	if (points == nullptr) {
		throw invalid_argument("The points of the ijk grid cannot be null.");
	}
	if (faceIndicesPerCell == nullptr) {
		throw invalid_argument("The definition of the face indices per cell is incomplete.");
	}
	if (nodeIndicesPerFace == nullptr) {
		throw invalid_argument("The definition of the node indices per face is incomplete.");
	}
	if (proxy == nullptr) {
		proxy = getRepository()->getDefaultHdfProxy();
		if (proxy == nullptr) {
			throw std::invalid_argument("A (default) HDF Proxy must be provided.");
		}
	}

	setConstantCellShapeGeometryUsingExistingDatasets(getHdfGroup() + "/CellFaceIsRightHanded", getHdfGroup() + "/Points",
		pointCount, faceCount, localCrs, proxy,
		getHdfGroup() + "/FacesPerCell", faceCountPerCell,
		getHdfGroup() + "/NodesPerFace", nodeCountPerFace);

	const ULONG64 cellCount = getCellCount();

	// HDF Face Right handness
	unsigned long long faceCountTmp = faceCountPerCell * cellCount; // For GCC : ULONG64 is not exactly an unsigned long long with GCC but an uint64_t {aka long unsigned int}
	proxy->writeArrayNd(getHdfGroup(), "CellFaceIsRightHanded", H5T_NATIVE_UCHAR, cellFaceIsRightHanded, &faceCountTmp, 1);

	// HDF Face indices
	hsize_t numValues[2];
	numValues[0] = cellCount;
	numValues[1] = faceCountPerCell;
	proxy->writeArrayNd(getHdfGroup(), "FacesPerCell", H5T_NATIVE_ULLONG, faceIndicesPerCell, numValues, 2);

	// HDF Node indices
	numValues[0] = faceCount;
	numValues[1] = nodeCountPerFace;
	proxy->writeArrayNd(getHdfGroup(), "NodesPerFace", H5T_NATIVE_ULLONG, nodeIndicesPerFace, numValues, 2);

	// HDF points
	numValues[0] = pointCount;
	numValues[1] = 3; // 3 for X, Y and Z
	proxy->writeArrayNdOfDoubleValues(getHdfGroup(), "Points", points, numValues, 2);
}

void UnstructuredGridRepresentation::setTetrahedraOnlyGeometryUsingExistingDatasets(const std::string& cellFaceIsRightHanded, const std::string& points,
	ULONG64 pointCount, ULONG64 faceCount, EML2_NS::AbstractHdfProxy* proxy,
	const std::string& faceIndicesPerCell, const std::string& nodeIndicesPerFace, RESQML2_NS::AbstractLocal3dCrs * localCrs)
{
	setConstantCellShapeGeometryUsingExistingDatasets(cellFaceIsRightHanded, points,
		pointCount, faceCount, localCrs, proxy,
		faceIndicesPerCell, 4,
		nodeIndicesPerFace, 3);
}

void UnstructuredGridRepresentation::setTetrahedraOnlyGeometry(unsigned char * cellFaceIsRightHanded, double * points, ULONG64 pointCount, ULONG64 faceCount, EML2_NS::AbstractHdfProxy * proxy,
	ULONG64 * faceIndicesPerCell, ULONG64 * nodeIndicesPerFace, RESQML2_NS::AbstractLocal3dCrs * localCrs)
{
	setConstantCellShapeGeometry(cellFaceIsRightHanded, points,
		pointCount, faceCount, localCrs, proxy,
		faceIndicesPerCell, 4,
		nodeIndicesPerFace, 3);
}

void UnstructuredGridRepresentation::setHexahedraOnlyGeometryUsingExistingDatasets(const std::string& cellFaceIsRightHanded, const std::string& points,
	ULONG64 pointCount, ULONG64 faceCount, EML2_NS::AbstractHdfProxy* proxy,
	const std::string& faceIndicesPerCell, const std::string& nodeIndicesPerFace, RESQML2_NS::AbstractLocal3dCrs * localCrs)
{
	setConstantCellShapeGeometryUsingExistingDatasets(cellFaceIsRightHanded, points,
		pointCount, faceCount, localCrs, proxy,
		faceIndicesPerCell, 6,
		nodeIndicesPerFace, 4);
}

void UnstructuredGridRepresentation::setHexahedraOnlyGeometry(unsigned char * cellFaceIsRightHanded, double * points, ULONG64 pointCount, ULONG64 faceCount, EML2_NS::AbstractHdfProxy * proxy,
	ULONG64 * faceIndicesPerCell, ULONG64 * nodeIndicesPerFace, RESQML2_NS::AbstractLocal3dCrs * localCrs)
{
	setConstantCellShapeGeometry(cellFaceIsRightHanded, points,
		pointCount, faceCount, localCrs, proxy,
		faceIndicesPerCell, 6,
		nodeIndicesPerFace, 4);
}

void UnstructuredGridRepresentation::loadGeometry()
{
	unloadGeometry();

	if (isNodeCountOfFacesConstant() == true)
	{
		constantNodeCountPerFace = getConstantNodeCountOfFaces();
		nodeIndicesOfFaces.reset(new ULONG64[constantNodeCountPerFace * getFaceCount()]);
	}
	else
	{
		cumulativeNodeCountPerFace.reset(new ULONG64[getFaceCount()]);
		getCumulativeNodeCountPerFace(cumulativeNodeCountPerFace.get());
		nodeIndicesOfFaces.reset(new ULONG64[cumulativeNodeCountPerFace[getFaceCount() - 1]]);
	}

	if (isFaceCountOfCellsConstant() == true)
	{
		constantFaceCountPerCell = getConstantFaceCountOfCells();
		faceIndicesOfCells.reset(new ULONG64[constantFaceCountPerCell * getCellCount()]);
	}
	else
	{
		cumulativeFaceCountPerCell.reset(new ULONG64[getCellCount()]);
		getCumulativeFaceCountPerCell(cumulativeFaceCountPerCell.get());
		faceIndicesOfCells.reset(new ULONG64[cumulativeFaceCountPerCell[getCellCount() - 1]]);
	}

	getNodeIndicesOfFaces(nodeIndicesOfFaces.get());

	getFaceIndicesOfCells(faceIndicesOfCells.get());
}

void UnstructuredGridRepresentation::unloadGeometry()
{
	constantNodeCountPerFace = 0;
	constantFaceCountPerCell = 0;

	cumulativeNodeCountPerFace.reset();
	cumulativeFaceCountPerCell.reset();
	nodeIndicesOfFaces.reset();
	faceIndicesOfCells.reset();
}

unsigned int UnstructuredGridRepresentation::getFaceCountOfCell(ULONG64 cellIndex) const
{
	if (cellIndex >= getCellCount())
		throw out_of_range("The cell index is out of range.");

	if (constantFaceCountPerCell != 0)
		return constantFaceCountPerCell;

	if (!faceIndicesOfCells)
		throw logic_error("The geometry must have been loaded first.");

	if (cellIndex == 0)
		return cumulativeFaceCountPerCell[0];
	
	return cumulativeFaceCountPerCell[cellIndex] -  cumulativeFaceCountPerCell[cellIndex-1];
}

ULONG64 UnstructuredGridRepresentation::getGlobalFaceIndex(ULONG64 cellIndex, unsigned int localFaceIndex) const
{
	if (!faceIndicesOfCells)
		throw invalid_argument("The geometry must have been loaded first.");
	if (cellIndex >= getCellCount())
		throw range_error("The cell index is out of range.");
	if (localFaceIndex >= getFaceCountOfCell(cellIndex))
		throw range_error("The face index is out of range.");

	ULONG64 globalFaceIndex = 0;
	if (cellIndex == 0)
		globalFaceIndex = faceIndicesOfCells[localFaceIndex];
	else
	{
		if (constantFaceCountPerCell != 0)
			globalFaceIndex = faceIndicesOfCells[constantFaceCountPerCell * cellIndex + localFaceIndex];
		else
			globalFaceIndex = faceIndicesOfCells[cumulativeFaceCountPerCell[cellIndex - 1] + localFaceIndex];
	}

	return globalFaceIndex;
}

unsigned int UnstructuredGridRepresentation::getNodeCountOfFaceOfCell(ULONG64 cellIndex, unsigned int localFaceIndex) const
{
	if (constantNodeCountPerFace != 0)
		return constantNodeCountPerFace;

	const ULONG64 globalFaceIndex = getGlobalFaceIndex(cellIndex, localFaceIndex);

	return globalFaceIndex == 0
		? cumulativeNodeCountPerFace[0]
		: cumulativeNodeCountPerFace[globalFaceIndex] - cumulativeNodeCountPerFace[globalFaceIndex - 1];
}

ULONG64 const * UnstructuredGridRepresentation::getNodeIndicesOfFaceOfCell(ULONG64 cellIndex, unsigned int localFaceIndex) const
{
	const ULONG64 globalFaceIndex = getGlobalFaceIndex(cellIndex, localFaceIndex);

	if (globalFaceIndex == 0)
		return nodeIndicesOfFaces.get();

	if (constantNodeCountPerFace != 0)
		return &(nodeIndicesOfFaces[constantNodeCountPerFace * globalFaceIndex]);
	else
		return &(nodeIndicesOfFaces[cumulativeNodeCountPerFace[globalFaceIndex - 1]]);
}
