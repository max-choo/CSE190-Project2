/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */

#include "btree.h"
#include "filescan.h"
#include "exceptions/bad_index_info_exception.h"
#include "exceptions/bad_opcodes_exception.h"
#include "exceptions/bad_scanrange_exception.h"
#include "exceptions/no_such_key_found_exception.h"
#include "exceptions/scan_not_initialized_exception.h"
#include "exceptions/index_scan_completed_exception.h"
#include "exceptions/file_not_found_exception.h"
#include "exceptions/end_of_file_exception.h"

#include <queue>
#include <cmath>

//#define DEBUG
/*
	switch(this->attributeType) {
		case INTEGER: break;
		case DOUBLE: break;
		case STRING: break;
		default: break;
	}
*/
class Empty{};

namespace badgerdb
{

// -----------------------------------------------------------------------------
// BTreeIndex::BTreeIndex -- Constructor
// -----------------------------------------------------------------------------

BTreeIndex::BTreeIndex(const std::string & relationName,
		std::string & outIndexName,
		BufMgr *bufMgrIn,
		const int attrByteOffset,
		const Datatype attrType)
{

	// Construct index name
	std::ostringstream idxStr;
	idxStr << relationName << '.' << attrByteOffset;
	outIndexName = idxStr.str();

	// If index file already exists, remove it first
	try {
		File::remove(outIndexName);
	}
	catch (FileNotFoundException &e) { }

	// Create/Open blobfile
	BlobFile *bfile;
	if(bfile->exists(outIndexName)) {
		bfile = new BlobFile(outIndexName, false);
		this->file = bfile;
	}
	else {
		bfile = new BlobFile(outIndexName, true);
		this->file = bfile;
	}

	// Set values for BTreeIndex
	this->bufMgr = bufMgrIn;
	this->attributeType = attrType;
	this->attrByteOffset = attrByteOffset;
	this->numOfNodes = 0;
	this->startScanIndex = -1;
	this->scanExecuting = false;

	// Construct metadata page
	Page * metaPage = new Page();
	this->bufMgr->allocPage(this->file, this->headerPageNum, metaPage);
	IndexMetaInfo * metadata = new IndexMetaInfo();
	metadata = (IndexMetaInfo*)metaPage;
	metadata->attrByteOffset = attrByteOffset;
	metadata->attrType = attrType;
	metadata->rootPageNo = 0;
	strcpy(metadata->relationName, relationName.c_str());
	this->bufMgr->unPinPage(this->file, this->headerPageNum, 1);

	std::cout << "CONST: scanexecuting is " << scanExecuting << std::endl;
	std::cout << "atttype is " << this->attributeType;
	// Insert all entries in relation into index
	FileScan fscan(relationName, this->bufMgr);

	// Specify leaf and nonleaf types
	LeafNodeInt* LEAFINTEGER = new LeafNodeInt();
	NonLeafNodeInt * NONLEAFINTEGER = new NonLeafNodeInt();
	LeafNodeDouble* LEAFDOUBLE = new LeafNodeDouble();
	NonLeafNodeDouble * NONLEAFDOUBLE = new NonLeafNodeDouble();
	LeafNodeString* LEAFSTRING = new LeafNodeString();
	NonLeafNodeString * NONLEAFSTRING = new NonLeafNodeString();

	try {
		while (true) {
			RecordId rid;
			fscan.scanNext(rid);
			std::string record = fscan.getRecord();


			switch(this->attributeType) {

				case INTEGER:
					this->insertEntry(LEAFINTEGER, NONLEAFINTEGER, *((int*)record.c_str()), rid);
					break;
				case DOUBLE: 
					this->insertEntry(LEAFDOUBLE, NONLEAFDOUBLE, (double) (*((int*)record.c_str())), rid);
					break;
				case STRING:
//					std::cout << (char*)((int*)record.c_str()) << '\n';
//					this->insertEntry(LEAFSTRING, NONLEAFSTRING, (*((char*)record.c_str())), rid);
					break;

				default: break;
			}
		}
	}
	catch(EndOfFileException &e) {	}

	delete(LEAFINTEGER);
	delete(NONLEAFINTEGER);
	delete(LEAFDOUBLE);
	delete(NONLEAFDOUBLE);
	delete(LEAFSTRING);
	delete(NONLEAFSTRING);;


}


// -----------------------------------------------------------------------------
// BTreeIndex::~BTreeIndex -- destructor
// -----------------------------------------------------------------------------

BTreeIndex::~BTreeIndex()
{
	std::cout << "destructor called" << std::endl;
	this->file->~File();
}

// -----------------------------------------------------------------------------
// BTreeIndex::insertEntry
// -----------------------------------------------------------------------------
template<typename T, typename L, typename NL>
const void BTreeIndex::insertEntry(L* leafType, NL* nonLeafType, T keyValue, const RecordId rid) 
{
//	int keyValue = *(int*)key;
	int leafSize;
	switch(this->attributeType) {
		case INTEGER: leafSize = INTARRAYLEAFSIZE; break;
		case DOUBLE: leafSize = DOUBLEARRAYLEAFSIZE; break;
		case STRING: leafSize = STRINGARRAYLEAFSIZE; break;
		default: break;
	}

	Page * metadataPage = new Page();
	this->bufMgr->readPage(this->file, this->headerPageNum, metadataPage);
	IndexMetaInfo * metadata = new IndexMetaInfo();
	metadata = (IndexMetaInfo*)metadataPage;

	/*  Tree is empty (first insertion) */
	if (metadata->rootPageNo == 0) {
		// Allocate root page
		Page * rootPage = new Page();
		this->bufMgr->allocPage(this->file, this->rootPageNum, rootPage);
		metadata->rootPageNo = this->rootPageNum;
		this->numOfNodes++;

		L * rootNode = new L();
		rootNode = (L*)rootPage;

		for (int i=0; i < leafSize; i++) {
			rootNode->keyArray[i] = -1;
		}
		rootNode->keyArray[0] = keyValue;
		rootNode->ridArray[0] = rid;
		this->bufMgr->unPinPage(this->file, this->rootPageNum, 1);
	}

	/* Tree is not empty (every cases other than first insertion) */
	else {

		/* This block handles the simple case where there is only one node.
 		 * What make this case differ from other cases are:
 		 * 1. Because there is only one node, root will be a leaf.
 		 *    So we cast the root to LeafNodeInt.
 		 * 2. We are not traversing through nodes. So parent node will be NULL.
 		 */
		if (this->numOfNodes == 1)  {
			Page * rootPage = new Page();
			this->bufMgr->readPage(this->file, this->rootPageNum, rootPage);
			L * rootNode = new L();
			rootNode = (L*)rootPage;


			// After the insertion, call fullNodeHandler if the node is full.
			switch(this->attributeType) {
				case INTEGER: insertToNode<int>(rootNode, keyValue, rid); break;
				case DOUBLE: insertToNode<double>(rootNode, keyValue, rid); break;
				//case STRING: insertToNode<std::string>(rootNode, keyValue, rid); break;
				default: break;
			}
			if (rootNode->keyArray[leafSize-1] != -1) { 
				NL* dummy = new NL();
				fullNodeHandler(leafType, nonLeafType, rootNode, dummy, this->rootPageNum,1, 1);
				this->numOfNodes++;
				delete(dummy);
			}
		}

		/* This block is the general case where there is more than one node */
		else {	// more than one node

	//std::cout << "inserting " << keyValue << std::endl;
			Page * rootPage;
			bufMgr->readPage(file, metadata->rootPageNo, rootPage);  
			NL * rootNode = new NL();
			rootNode = (NL*)rootPage;

			traverse(leafType, nonLeafType, rootNode, keyValue, rid);
			if (rootNode->keyArray[leafSize-1] != -1) {
				std::cout << "Full when inserting " << keyValue << std::endl;
				NL* dummy = new NL();
				fullNodeHandler(leafType, nonLeafType, rootNode, dummy, this->rootPageNum, 1, 1);
				this->numOfNodes++;
				delete(dummy);
			}
/*
 *			traverse from root: traverse(currNode)
 *			currNode is initially root.
 *
 *			In traverse(), find the child to traverse. traverse(childNode)
 *			traverse() has two instances: currNode and childNode. In the next call, currNode will be the parent of childNode.
 *
 *			traverse() will be implemented as following:
 *			Get childNode
 *			If no childNode, return
 *			traverse(childNode)
 *			check if childNode is full (if exists)
 *				if so, call fullNodeHandler(currNode, childNode, ... )
 */
			
		}

	}
	
	metadata->rootPageNo = this->rootPageNum;
	this->bufMgr->unPinPage(this->file, this->headerPageNum, 0);
	//if (keyValue == 4999)	testPrint();
}


// -----------------------------------------------------------------------------
// BTreeIndex::insertToNode - helpfer function of insertEntry
// -----------------------------------------------------------------------------
template <typename T, typename L>
const void BTreeIndex::insertToNode(L * node, T keyValue, const RecordId rid) {	

	int leafSize;
	switch(this->attributeType) {
		case INTEGER: leafSize = INTARRAYLEAFSIZE; break;
		case DOUBLE: leafSize = DOUBLEARRAYLEAFSIZE; break;
		case STRING: leafSize = STRINGARRAYLEAFSIZE; break;
		default: break;
	}

	int i = leafSize;

	// Push every values to the right when it's greater than key
	while (i > 0 && (keyValue < node->keyArray[i-1] || node->keyArray[i-1] == -1)) {
		if (node->keyArray[i-1] != -1) {
			node->keyArray[i] = node->keyArray[i-1];
			node->ridArray[i] = node->ridArray[i-1];
		}
		i--;
	}

	node->keyArray[i] = keyValue;
	node->ridArray[i] = rid;
//	std::cout << rid.page_number;
}

// -----------------------------------------------------------------------------
// BTreeIndex::fullNodeHandler - helper function of insertEntry
// -----------------------------------------------------------------------------
template<typename L, typename NL>
void BTreeIndex::fullNodeHandler(L* leafType, NL* nonLeafType, void* currNode, NL *parentNode, PageId currPageNo, bool isLeaf, bool isRoot) {

	int nonleafSize;
	switch(this->attributeType) {
		case INTEGER: nonleafSize = INTARRAYNONLEAFSIZE; break;
		case DOUBLE: nonleafSize = DOUBLEARRAYNONLEAFSIZE; break;
		case STRING: nonleafSize = STRINGARRAYNONLEAFSIZE; break;
		default: break;
	}

	// Check if there already exists the parent to push up
//	if (parentNode) { 
	if (isRoot) {
		Page * newParentPage = new Page();
		this->bufMgr->allocPage(this->file, this->rootPageNum, newParentPage);
		parentNode = (NL*)newParentPage;
		for (int i=0; i < nonleafSize; i++) {
			parentNode->keyArray[i] = -1;
		}
		parentNode->level = 1;
	}

	
	// split page
	PageId rightPageNum;
	double middleKey;
	if (isLeaf) {
		L* currNodeLeaf = (L*)currNode;
		splitLeafNode(currNodeLeaf, middleKey, rightPageNum); // splitNode called
//		parentNode->level ++;
	}
	else{
		NL* currNodeNonLeaf = (NL*)currNode;
		splitNonLeafNode(currNodeNonLeaf, middleKey, rightPageNum);
//		parentNode->level ++;
	}
	std::cout << "middle key is " << middleKey << std::endl;

	// Set attribute for new root
	// Push every values to the right when it's greater than key
	int nonLeafIndex = nonleafSize;
	while (nonLeafIndex > 0 && (middleKey < parentNode->keyArray[nonLeafIndex-1] || parentNode->keyArray[nonLeafIndex-1] == -1)) {
		if (parentNode->keyArray[nonLeafIndex-1] != -1) {
			parentNode->keyArray[nonLeafIndex] = parentNode->keyArray[nonLeafIndex-1];
			parentNode->pageNoArray[nonLeafIndex+1] = parentNode->pageNoArray[nonLeafIndex];
			parentNode->pageNoArray[nonLeafIndex] = parentNode->pageNoArray[nonLeafIndex-1];

		}
		nonLeafIndex--;
	}
	std::cout << "leaf index is " << nonLeafIndex << std::endl;
	parentNode->keyArray[nonLeafIndex] = middleKey;
	parentNode->pageNoArray[nonLeafIndex] = currPageNo;
	parentNode->pageNoArray[nonLeafIndex+1] = rightPageNum;
	

//	testPrint();

}

// Just for test. Print whatever you want
void BTreeIndex::testPrint() {

	std::cout << "testPrint caled" << std::endl;
				/* TEST */

				Page * testRoot = new Page();
				this->bufMgr->readPage(this->file, this->rootPageNum, testRoot);
				NonLeafNodeDouble * testRootNode = new NonLeafNodeDouble();
				testRootNode = (NonLeafNodeDouble*)testRoot;

				// get left page
				Page * testLeft = new Page();
				this->bufMgr->readPage(this->file, testRootNode->pageNoArray[0], testLeft);
				LeafNodeDouble * testLeftNode = new LeafNodeDouble();
				testLeftNode = (LeafNodeDouble*)testLeft;
				
				Page * testRight = new Page();
				this->bufMgr->readPage(this->file, testRootNode->pageNoArray[1], testRight);
				LeafNodeDouble * testRightNode = new LeafNodeDouble();
				testRightNode = (LeafNodeDouble*)testRight;

				std::cout << "MIDDLE KEY IS " << testRootNode->keyArray[0] << std::endl;
				std::cout << "Level of root is " << testRootNode->level << std::endl;
				std::cout << "Number of nodes is " << numOfNodes << std::endl;

				std::cout << "PRINTING ROOT" << std::endl;
				for (int i=0; i < DOUBLEARRAYNONLEAFSIZE; i++) {
					if (testRootNode->keyArray[i] != -1) std::cout << testRootNode->keyArray[i] << ' ';
				}
				std::cout << std::endl;

			
				std::cout << "PRINTING LEFTEST (index 0)" << std::endl;
				for (int i=0; i < DOUBLEARRAYLEAFSIZE; i++) {
					if (testLeftNode->keyArray[i] != -1) {
						std::cout << testLeftNode->keyArray[i] << ' ';
					}	
			}
			std::cout << "rightsib no is  " << testLeftNode->rightSibPageNo << std::endl;
				std::cout << std::endl;
				std::cout << "PRINTING Page with index 1" << std::endl;
				for (int i=0; i < DOUBLEARRAYLEAFSIZE; i++) {
					if (testRightNode->keyArray[i] != -1) {
						std::cout << testRightNode->keyArray[i] << ' ';
						std::cout << "page_num " << testRightNode->ridArray[i].page_number << ' ';
					}	
				}
			std::cout << "rightsib no is  " << testRightNode->rightSibPageNo << std::endl;
		for (int j=2; j < 100; j++) {
			if (testRootNode->pageNoArray[j] == 0) { std::cout << "end of root " << std::endl; break; }
 Page * test3Right = new Page();
			std::cout << "now reading the page with pageno " << testRootNode->pageNoArray[j] << std::endl;
this->bufMgr->readPage(this->file, testRootNode->pageNoArray[j], test3Right);
LeafNodeDouble * test3RightNode = new LeafNodeDouble();
test3RightNode = (LeafNodeDouble*)test3Right;
				std::cout << std::endl;
				std::cout << "PRINTING Page with index " << j << std::endl;
				for (int i=0; i < DOUBLEARRAYLEAFSIZE; i++) {
					if (test3RightNode->keyArray[i] != -1) {
						std::cout << test3RightNode->keyArray[i] << ' ';
					}	

				}
				std::cout << "right sib no is " << test3RightNode->rightSibPageNo << std::endl;
		}

		for (int i=0; i < 10; i++ ) std::cout << testRootNode->pageNoArray[i] << ' ';
}

// -----------------------------------------------------------------------------
// BTreeIndex::splitLeafNode - helper function of insertEntry
// -----------------------------------------------------------------------------
template <typename T, typename L>
void BTreeIndex::splitLeafNode(L *& leftNode, T& middleKey, PageId &pid) { 

	int leafSize;
	switch(this->attributeType) {
		case INTEGER: leafSize = INTARRAYLEAFSIZE; break;
		case DOUBLE: leafSize = DOUBLEARRAYLEAFSIZE; break;
		case STRING: leafSize = STRINGARRAYLEAFSIZE; break;
		default: break;
	}

	// After splitNode, the original node will be in left, while returned node will be in right
/*
	for (int i=0; i < DOUBLEARRAYLEAFSIZE; i++) {
		std::cout << "left node key is " << leftNode->keyArray[i];
	}
*/
	// Allocate new page to be right child of root
	Page * rightPage = new Page();
	this->bufMgr->allocPage(this->file, pid, rightPage);
	L * rightNode = new L();
	rightNode = (L*)rightPage;

	// get the middle value
	int middlePoint = (leafSize/2);
	middleKey = leftNode->keyArray[middlePoint];
	std::cout << "Splitting with midpoint " << middleKey <<std::endl;
	// move right side of original node to rightNode
//	std::cout << "klajfsej " << leftNode->keyArray[middlePoint-1];
	for (int i=middlePoint, j=0; i < leafSize; i++, j++) {
		rightNode->keyArray[j] = leftNode->keyArray[i];
		rightNode->ridArray[j] = leftNode->ridArray[i];
	}

	for (int i=middlePoint; i < leafSize; i++) {
		if (leafSize % 2 == 1 && i == middlePoint) {}
		else { rightNode->keyArray[i] = -1; }
		leftNode->keyArray[i] = -1;
	}

	if (leftNode->rightSibPageNo != 0) rightNode->rightSibPageNo = leftNode->rightSibPageNo;
	leftNode->rightSibPageNo = pid;

/*
	std::cout << "LEFTNODE PRINTING\n";
	for (int i=0; i <middlePoint; i++) std::cout << leftNode->ridArray[i].page_number << ' '; 
	std::cout << "RIGHTNODE PRINTING\n";
	for (int i=0; i <middlePoint; i++) std::cout << rightNode->ridArray[i].page_number << ' '; 
*/	// Increment total number of nodes 
	this->numOfNodes += 1;


}

// -----------------------------------------------------------------------------
// BTreeIndex::splitNonLeafNode - helper function of insertEntry
// -----------------------------------------------------------------------------
template <typename T, typename NL>
void BTreeIndex::splitNonLeafNode(NL *& leftNode, T& middleKey, PageId &pid) { // TO DO: Add implementation 

	int nonleafSize;
	switch(this->attributeType) {
		case INTEGER: nonleafSize = INTARRAYNONLEAFSIZE; break;
		case DOUBLE: nonleafSize = DOUBLEARRAYNONLEAFSIZE; break;
		case STRING: nonleafSize = STRINGARRAYNONLEAFSIZE; break;
		default: break;
	}

	Page * rightPage = new Page();
	this->bufMgr->allocPage(this->file, pid, rightPage);
	NL * rightNode = new NL();
	rightNode = (NL*)rightPage;

	// get the middle value
	int middlePoint = nonleafSize/2;
	middleKey = leftNode->keyArray[middlePoint];

	// move right side of original node to rightNode
	for (int i=middlePoint, j=0; i < nonleafSize; i++, j++) {
		rightNode->keyArray[j] = leftNode->keyArray[i];
		rightNode->pageNoArray[j] = leftNode->pageNoArray[i];
	}
	for (int i=middlePoint; i < nonleafSize; i++) {
		rightNode->keyArray[i] = -1;
		leftNode->keyArray[i] = -1;
	}

	// Increment total number of nodes 
	this->numOfNodes += 1;
}

// -----------------------------------------------------------------------------
// BTreeIndex::traverse - helper function of insertEntry
// -----------------------------------------------------------------------------
template <typename T, typename L, typename NL>
void BTreeIndex::traverse(L* leafType, NL* nonLeafType, NL* currNode, T key, const RecordId rid) { 

	int leafSize;
	int nonleafSize;
	switch(this->attributeType) {
		case INTEGER: leafSize = INTARRAYLEAFSIZE; nonleafSize = INTARRAYNONLEAFSIZE; break;
		case DOUBLE: leafSize = DOUBLEARRAYLEAFSIZE; nonleafSize = DOUBLEARRAYNONLEAFSIZE; break;
		case STRING: leafSize = STRINGARRAYLEAFSIZE; nonleafSize = STRINGARRAYNONLEAFSIZE; break;
		default: break;
	}
//	int keyValue = *(int*)key;

	int i = 0;
	for(i = 0; i < nonleafSize; i++){
		if(currNode->pageNoArray[i+1] == 0 || currNode->keyArray[i] > key){
			break;
		}
	}

	Page * childPage = new Page();
//	std::cout << "Seeking " << i << "th index with pag no" << currNode->pageNoArray[i] << std::endl;
	this->bufMgr->readPage(this->file, currNode->pageNoArray[i], childPage);

	void * childNode;
	if (currNode->level != 1) {		// child is non-leaf
		childNode = (NL*)childPage;

		traverse(leafType, nonLeafType, (NL*)childNode, key, rid);

		if (currNode->keyArray[nonleafSize] != -1) { 
			fullNodeHandler(leafType, nonLeafType, childNode, currNode, currNode->pageNoArray[i], 1, 0);
			this->numOfNodes++;
		}
	}
	else {		// child is leaf
		childNode = (L*)childPage;
//		insertToNode<int>((LeafNodeInt*)childNode, key, rid);
		switch(this->attributeType) {
			case INTEGER: insertToNode<int>((L*)childNode, key, rid); break;
			case DOUBLE: insertToNode<double>((L*)childNode, key, rid); break;
		//	case STRING: insertToNode<std::string>((LeafNodeInt*)childNode, key, rid); break;
			default: break;
		}

		if (((L*)childNode)->keyArray[leafSize-1] != -1) { 

/*			for (int i=0; i< INTARRAYLEAFSIZE; i++) {
				std::cout << "At index " << i << ": " << ((LeafNodeInt*)childNode)->keyArray[i] << std::endl;
			}
*/
//			testPrint();
//			std::cout << "Full when inserting " << keyValue << std::endl;
			fullNodeHandler(leafType, nonLeafType, childNode, currNode, currNode->pageNoArray[i],1, 0);
			this->numOfNodes++;

			//testPrint();

		}
	}





/*
	if(currNode->level == 1){

		bufMgr->unPinPage(file, currNodeId, false);
		Page * leafPage;
		bufMgr->readPage(file, currNode->pageNoArray[i],leafPage);
		return (LeafNodeInt *) leafPage; 
	}
	else{	
		// Start scanning the node index
		int i = 0;
		for(i = 0; i < INTARRAYNONLEAFSIZE; i++){
			if(currNode->keyArray[i] != 0 && currNode->keyArray[i] > key){
				break;
			}
		}
		//PageId childPageNo = parentNode->pageNoArray[i];
		bufMgr->unPinPage(file, currNodeId, false);
		Page * parent;
		bufMgr->readPage(file, currNode->pageNoArray[i], parent);
		return traverse(key, (NonLeafNodeInt *) parent, currNode->pageNoArray[i]);
*/
}

template <typename T, typename L, typename NL>
const void BTreeIndex::startScanGeneric (L* leafType, NL* nonLeafType, T lowVal, T highVal)
{

	int leafSize;
	int nonleafSize;
	switch(this->attributeType) {
		case INTEGER:
			leafSize = INTARRAYLEAFSIZE;
			nonleafSize = INTARRAYNONLEAFSIZE; 
			break;
		case DOUBLE:
			leafSize = DOUBLEARRAYLEAFSIZE;
			nonleafSize = DOUBLEARRAYNONLEAFSIZE;
			break;
		case STRING:
			leafSize = STRINGARRAYLEAFSIZE;
			nonleafSize = STRINGARRAYNONLEAFSIZE;
			break;
		default: break;
	}

	Page* metaPage; 
	bufMgr->readPage(file, headerPageNum, metaPage); 

	IndexMetaInfo * metadata = (IndexMetaInfo *) metaPage;

	
	if(lowVal > highVal) {
		throw BadScanrangeException();
	}

	scanExecuting = true;

	std::cout << "lowValParm: " << lowVal << ", highValParm: " << highVal << "\n";

	if(numOfNodes == 1){
		std::cout << "numOfNodes = 1\n";
		currentPageNum = metadata->rootPageNo;
		nextEntry = 0;
		return;
	} 


	else {
		std::cout << "num of nodes = " << numOfNodes << "\n";
		// Get the root node.
		bufMgr->readPage(file, metadata->rootPageNo, currentPageData); 
		NL * currNode  = (NL *) currentPageData;
//		NonLeafNodeDouble * currNode  = (NonLeafNodeDouble *) currentPageData;
		currentPageNum = metadata->rootPageNo;	
		std::cout <<"curNode->level: " <<  currNode->level << "\n";		
	
		int index = 0;
		while(currNode->level != 1){

			std::cout <<"curNode->level: " <<  currNode->level << "\n";		
			for(index = 0; index < nonleafSize; index++){ 	
				if(lowVal <= currNode->keyArray[index] && currNode->keyArray[index] != -1){
					//index++;
					break;
				}
			}

			bufMgr->readPage(file, currNode->pageNoArray[index], currentPageData);
			bufMgr->unPinPage(file, currentPageNum, false);
			currNode = (NL *) currentPageData;
//			currNode = (NonLeafNodeDouble *) currentPageData;
			currentPageNum = currNode->pageNoArray[index];
		}

		// At the 1st level node 
		for(index = 0; index < nonleafSize; index++){ 	
			std::cout << currNode->keyArray[index] << ", ";
			if(lowVal <= currNode->keyArray[index] && currNode->keyArray[index] != -1){
				//index++;
				break;
			}
		}

		std::cout << "\n";

		std::cout << "Index: " << index << "\n";	
		bufMgr->readPage(file, currNode->pageNoArray[index], currentPageData);
		bufMgr->unPinPage(file, currentPageNum, false);
		currentPageNum = currNode->pageNoArray[index];
		nextEntry = 0;

		
		std::cout << "Printing leaf nodes\n";
		L * currNode2 = (L *) currentPageData;
//		LeafNodeDouble * currNode2 = (LeafNodeDouble *) currentPageData;
		for(index = 0; index < leafSize; index++){
			if(currNode2->keyArray[index] != -1)
				std::cout << currNode2->keyArray[index] << ", ";
		}	
	}
}
// -----------------------------------------------------------------------------
// BTreeIndex::startScan
// -----------------------------------------------------------------------------
const void BTreeIndex::startScan(const void* lowValParm,
				   const Operator lowOpParm,
				   const void* highValParm,
				   const Operator highOpParm)
{



	//std::cout << "In startScan\n";

	// Operator: LT, LTE, GTE, GT
	if(lowOpParm == LT || lowOpParm == LTE || highOpParm == GT || highOpParm == GTE){
		throw BadOpcodesException();
	} 
	
	lowOp = lowOpParm;
	highOp = highOpParm;

	// Specify leaf and nonleaf types
	LeafNodeInt* LEAFINTEGER = new LeafNodeInt();
	NonLeafNodeInt * NONLEAFINTEGER = new NonLeafNodeInt();
	LeafNodeDouble* LEAFDOUBLE = new LeafNodeDouble();
	NonLeafNodeDouble * NONLEAFDOUBLE = new NonLeafNodeDouble();
	LeafNodeString* LEAFSTRING = new LeafNodeString();
	NonLeafNodeString * NONLEAFSTRING = new NonLeafNodeString();

	switch(this->attributeType) {
		case INTEGER:
			lowValInt = *((int *) lowValParm);
			highValInt = *((int*) highValParm);
			startScanGeneric(LEAFINTEGER, NONLEAFINTEGER, lowValInt, highValInt);
			break;
		case DOUBLE:
			lowValDouble = *((double *) lowValParm);
			highValDouble = *((double*) highValParm);
			startScanGeneric(LEAFDOUBLE, NONLEAFDOUBLE, lowValDouble, highValDouble);
			break;
/*		case STRING:
			lowValString = *((double *) lowValParm);
			highValString = *((double*) highValParm);
			startScanGeneric(lowValParm, lowOpParm, highValParm, highOpParm, nls, ls, lowValString, highValString);
			break;
*/		default: break;
	}

	delete(LEAFINTEGER);
	delete(NONLEAFINTEGER);
	delete(LEAFDOUBLE);
	delete(NONLEAFDOUBLE);
	delete(LEAFSTRING);
	delete(NONLEAFSTRING);;

}

template <typename T, typename L>
const void BTreeIndex::scanNextGeneric(L* leafType, RecordId& outRid, T lowVal, T highVal)
{

	int leafSize;
	switch(this->attributeType) {
		case INTEGER: leafSize = INTARRAYLEAFSIZE; break;
		case DOUBLE: leafSize = DOUBLEARRAYLEAFSIZE; break;
		case STRING: leafSize = STRINGARRAYLEAFSIZE; break;
		default: break;
	}
			
	if(!scanExecuting){
		throw ScanNotInitializedException();
	}

	bufMgr->readPage(file, currentPageNum, currentPageData); 
	L * currNode = (L *) currentPageData;
//	LeafNodeDouble * currNode = (LeafNodeDouble *) currentPageData;

	/*
	int index;	
	for(index = 0; index < INTARRAYLEAFSIZE; index++){
		if(currNode->keyArray[index] != -1)
			std::cout << currNode->keyArray[index] << ", ";
	}*/

	if(startScanIndex == -1){
		int i = 0;
		for(i = nextEntry; i < leafSize; i++){

			if(lowVal <= currNode->keyArray[i]){
				
				if((lowOp == GTE) && (lowVal == currNode->keyArray[i])){
					startScanIndex = i;
					break;	
				}
				else if((lowOp == GT) && (lowVal == currNode->keyArray[i])) {
					startScanIndex = i+1;
					break;
				}
				else{
					startScanIndex = i;
					break;
				}
			} 
		}
		//std::cout << "Broke out of the loop at i= " << i << "\n";
		nextEntry = startScanIndex;
	}
	
//	std::cout << "nextEntry: " << nextEntry << "\n";

	bool notFound = true;
	while(notFound){

	
	
/*		if(currNode->ridArray[nextEntry].page_number == 0){
			std::cout << "Scan completed page_number = 0\n";
			std::cout << "at index: " << nextEntry << "\n";
			throw IndexScanCompletedException();	
		}
*/
		if(currNode->keyArray[nextEntry] == -1){
			std::cout << "calling rightsibpagno\n";
			PageId siblingNode = currNode->rightSibPageNo;
			bufMgr->unPinPage(file, currentPageNum, false);	
		
			nextEntry = 0;
			if(siblingNode == 0){
				//std::cout << "Scan completed siblingNode = 0\n";
				throw IndexScanCompletedException();
			}

			currentPageNum = siblingNode;
			std::cout << "rigth sib no is " << currentPageNum << std::endl;
			bufMgr->readPage(file, currentPageNum, currentPageData);
			currNode = (L*)currentPageData;
		}

		else if (currNode->keyArray[nextEntry] > highVal){
			//std::cout << "Scan completed keyArray[nextEntry] > highValInt\n";
			//std::cout << "at index: " << nextEntry << "\n";
			throw IndexScanCompletedException();
		}

		else if(highVal >= currNode->keyArray[nextEntry]){
			//std::cout << "highValInt: " << highValInt << ", keyArray: " << currNode->keyArray[nextEntry];
			//std::cout << "\n";
			if((highOp == LTE) && (highVal == currNode->keyArray[nextEntry])){
				//throw IndexScanCompletedException();
				outRid = currNode->ridArray[nextEntry];
				notFound = false;
				nextEntry++;
			}
			else if((highOp == LT) && (highVal == currNode->keyArray[nextEntry])){
				//std::cout << "Scan completed highValInt = keyArray[nextEntry]\n";
				//std::cout << "at index: " << nextEntry << "\n";
				throw IndexScanCompletedException();
			}
			else{ 
				outRid = currNode->ridArray[nextEntry];
//				std::cout << "Found: " << currNode->keyArray[nextEntry] << "\n";
				//std::cout << "page_number: " << outRid.page_number; 
				notFound = false;
				nextEntry++;
			}	
		}
	}

}
// -----------------------------------------------------------------------------
// BTreeIndex::scanNext
// -----------------------------------------------------------------------------

const void BTreeIndex::scanNext(RecordId& outRid) 
{
	LeafNodeInt * LEAFINTEGER = new LeafNodeInt();
	LeafNodeDouble * LEAFDOUBLE = new LeafNodeDouble();
	LeafNodeString * LEAFSTRING = new LeafNodeString();
	

	switch(this->attributeType) {
		case INTEGER:
			scanNextGeneric(LEAFINTEGER, outRid, lowValInt, highValInt);
			break;
		case DOUBLE:
			scanNextGeneric(LEAFDOUBLE, outRid, lowValDouble, highValDouble);
			break;
/*		case STRING:
			LeafNodeString * ls;
			scanNextGeneric(outRid, ls, lowValString, highValString);
			break;
*/		default: break;
	}

	delete(LEAFINTEGER);
	delete(LEAFDOUBLE);
	delete(LEAFSTRING);
	//std::cout << "\nOut of scanNext()\n";

}

// -----------------------------------------------------------------------------
// BTreeIndex::endScan
// -----------------------------------------------------------------------------
//
const void BTreeIndex::endScan() 
{
	if(scanExecuting == false){
		throw ScanNotInitializedException();
	}
	scanExecuting = false;
	startScanIndex = -1;
}


void BTreeIndex::printTree(){

	std::cout << "PRINTING TREE..\n";	
	std::queue<PageId> q;

	Page* metaPage; 
	bufMgr->readPage(file, headerPageNum, metaPage); 
	IndexMetaInfo * metadata = (IndexMetaInfo *) metaPage;


	PageId currPageNum;
	LeafNodeInt * currLeafNode;
	Page* currPageData;
	if(numOfNodes == 1){
		bufMgr->readPage(file, metadata->rootPageNo, currPageData); 
		currLeafNode  = (LeafNodeInt *) currPageData;
		int i = 0;
		for(i = 0; i < INTARRAYNONLEAFSIZE; i++){
		 	if(currLeafNode->keyArray[i] != -1){
				std::cout << currLeafNode->keyArray[i] << ", ";
			}else{
				break;
			}
		}
		std::cout << "\n";
		return;
	}


	bufMgr->readPage(file, metadata->rootPageNo, currPageData); 
	NonLeafNodeInt * currNode  = (NonLeafNodeInt *) currPageData;
		
	//Only works for one level tree as for now	
	while(currNode->level >=1){

		int i = 0;
		for(i = 0; i < INTARRAYNONLEAFSIZE; i++){
		 	if(currNode->keyArray[i] != -1){
				std::cout << currNode->keyArray[i] << ", ";
				q.push(currNode->pageNoArray[i]);
			}else{
				break;
			}
		}
		std::cout << "\n";

		std::cout << "Leaf Nodes: \n";

		while(!q.empty()){
			currPageNum = q.front();
			q.pop();
			bufMgr->readPage(file, currPageNum, currPageData);
			currLeafNode = (LeafNodeInt *) currPageData;
		
			for(i = 0; i < INTARRAYLEAFSIZE; i++){
				if(currLeafNode->keyArray[i] != -1){
					std::cout << currLeafNode->keyArray[i] << ", ";		
				}
				else{
					break;
				}
			}	

			std::cout << "\n";
		}
		
	}

}
}
