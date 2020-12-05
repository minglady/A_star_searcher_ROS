#include "Astar_searcher.h"
#include <random>
using namespace std;
using namespace Eigen;

void AstarPathFinder::initGridMap(double _resolution, Vector3d global_xyz_l, Vector3d global_xyz_u, int max_x_id, int max_y_id, int max_z_id)
{   
    gl_xl = global_xyz_l(0);
    gl_yl = global_xyz_l(1);
    gl_zl = global_xyz_l(2);

    gl_xu = global_xyz_u(0);
    gl_yu = global_xyz_u(1);
    gl_zu = global_xyz_u(2);
    
    GLX_SIZE = max_x_id;
    GLY_SIZE = max_y_id;
    GLZ_SIZE = max_z_id;
    GLYZ_SIZE  = GLY_SIZE * GLZ_SIZE;
    GLXYZ_SIZE = GLX_SIZE * GLYZ_SIZE;

    resolution = _resolution;
    inv_resolution = 1.0 / _resolution;    

    data = new uint8_t[GLXYZ_SIZE];
    memset(data, 0, GLXYZ_SIZE * sizeof(uint8_t));//申请内存并初始化为0
    
    GridNodeMap = new GridNodePtr ** [GLX_SIZE];
    //构建节点三维地图
    for(int i = 0; i < GLX_SIZE; i++){
        GridNodeMap[i] = new GridNodePtr * [GLY_SIZE];
        for(int j = 0; j < GLY_SIZE; j++){
            GridNodeMap[i][j] = new GridNodePtr [GLZ_SIZE];
            for( int k = 0; k < GLZ_SIZE;k++){
                Vector3i tmpIdx(i,j,k);
                Vector3d pos = gridIndex2coord(tmpIdx);
                GridNodeMap[i][j][k] = new GridNode(tmpIdx, pos);
            }
        }
    }
}

void AstarPathFinder::resetGrid(GridNodePtr ptr)
{
    ptr->id = 0;
    ptr->cameFrom = NULL;
    ptr->gScore = inf;
    ptr->fScore = inf;
}

void AstarPathFinder::resetUsedGrids()//将节点地图初始化
{   
    for(int i=0; i < GLX_SIZE ; i++)
        for(int j=0; j < GLY_SIZE ; j++)
            for(int k=0; k < GLZ_SIZE ; k++)
                resetGrid(GridNodeMap[i][j][k]);
}

void AstarPathFinder::setObs(const double coord_x, const double coord_y, const double coord_z)
{   
    if( coord_x < gl_xl  || coord_y < gl_yl  || coord_z <  gl_zl || 
        coord_x >= gl_xu || coord_y >= gl_yu || coord_z >= gl_zu )
        return;

    int idx_x = static_cast<int>( (coord_x - gl_xl) * inv_resolution);
    int idx_y = static_cast<int>( (coord_y - gl_yl) * inv_resolution);
    int idx_z = static_cast<int>( (coord_z - gl_zl) * inv_resolution);      

    data[idx_x * GLYZ_SIZE + idx_y * GLZ_SIZE + idx_z] = 1;
}

vector<Vector3d> AstarPathFinder::getVisitedNodes()
{   
    vector<Vector3d> visited_nodes;
    for(int i = 0; i < GLX_SIZE; i++)
        for(int j = 0; j < GLY_SIZE; j++)
            for(int k = 0; k < GLZ_SIZE; k++){   
                //if(GridNodeMap[i][j][k]->id != 0) // visualize all nodes in open and close list
                if(GridNodeMap[i][j][k]->id == -1)  // visualize nodes in close list only
                    visited_nodes.push_back(GridNodeMap[i][j][k]->coord);//将处于闭集合中访问节点在真实地图中显示出来
            }

    ROS_WARN("visited_nodes size : %d", visited_nodes.size());
    return visited_nodes;//visited_nodes存储的是闭集合节点在真实地图中的位置
}

Vector3d AstarPathFinder::gridIndex2coord(const Vector3i & index) //将数值索引转换为真实地图位置索引
{
    Vector3d pt;

    pt(0) = ((double)index(0) + 0.5) * resolution + gl_xl;
    pt(1) = ((double)index(1) + 0.5) * resolution + gl_yl;
    pt(2) = ((double)index(2) + 0.5) * resolution + gl_zl;

    return pt;
}

Vector3i AstarPathFinder::coord2gridIndex(const Vector3d & pt) //将真实地图中的位置转换为索引值
{
    Vector3i idx;
    idx <<  min( max( int( (pt(0) - gl_xl) * inv_resolution), 0), GLX_SIZE - 1),
            min( max( int( (pt(1) - gl_yl) * inv_resolution), 0), GLY_SIZE - 1),
            min( max( int( (pt(2) - gl_zl) * inv_resolution), 0), GLZ_SIZE - 1);                  
  
    return idx;
}

Eigen::Vector3d AstarPathFinder::coordRounding(const Eigen::Vector3d & coord)//将真实地图中的采样点转换为索引地图中可访问的真实地图近似点
{
    return gridIndex2coord(coord2gridIndex(coord));
}

inline bool AstarPathFinder::isOccupied(const Eigen::Vector3i & index) const
{
    return isOccupied(index(0), index(1), index(2));
}

inline bool AstarPathFinder::isFree(const Eigen::Vector3i & index) const
{
    return isFree(index(0), index(1), index(2));
}

inline bool AstarPathFinder::isOccupied(const int & idx_x, const int & idx_y, const int & idx_z) const 
{
    return  (idx_x >= 0 && idx_x < GLX_SIZE && idx_y >= 0 && idx_y < GLY_SIZE && idx_z >= 0 && idx_z < GLZ_SIZE && 
            (data[idx_x * GLYZ_SIZE + idx_y * GLZ_SIZE + idx_z] == 1));
}

inline bool AstarPathFinder::isFree(const int & idx_x, const int & idx_y, const int & idx_z) const 
{
    return (idx_x >= 0 && idx_x < GLX_SIZE && idx_y >= 0 && idx_y < GLY_SIZE && idx_z >= 0 && idx_z < GLZ_SIZE && 
           (data[idx_x * GLYZ_SIZE + idx_y * GLZ_SIZE + idx_z] < 1));
}

inline void AstarPathFinder::AstarGetSucc(GridNodePtr currentPtr, vector<GridNodePtr> & neighborPtrSets, vector<double> & edgeCostSets)
{   
    neighborPtrSets.clear();
    edgeCostSets.clear();
    int idx_x = currentPtr->index(0);
    int idx_y = currentPtr->index(1);
    int idx_z = currentPtr->index(2);
    vector<Eigen::Vector3i> data_idx;
    vector<Eigen::Vector3i>::iterator it;
    /*//假定六连通
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y+1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y,idx_z+1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y-1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y,idx_z-1));
    for(it = data_idx.begin();it!=data_idx.end();it++)
    {
    	if(isFree(*it))
    	{
    		neighborPtrSets.push_back(GridNodeMap[(*it)(0)][(*it)(1)][(*it)(2)]);
    		edgeCostSets.push_back(1.0);
    	}
	}
	*/

	//二十六联通
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y+1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y,idx_z+1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y-1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y,idx_z-1));
    for(it = data_idx.begin();it!=data_idx.end();it++)
    {
    	if(isFree(*it))
    	{
    		neighborPtrSets.push_back(GridNodeMap[(*it)(0)][(*it)(1)][(*it)(2)]);
    		edgeCostSets.push_back(1.0);
    	}
	}
	data_idx.clear();
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y-1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y+1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y,idx_z+1));
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y,idx_z-1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y-1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y,idx_z-1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y+1,idx_z));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y,idx_z+1));
	data_idx.push_back(Eigen::Vector3i(idx_x,idx_y+1,idx_z+1));
    data_idx.push_back(Eigen::Vector3i(idx_x,idx_y+1,idx_z-1));
    data_idx.push_back(Eigen::Vector3i(idx_x,idx_y-1,idx_z+1));
    data_idx.push_back(Eigen::Vector3i(idx_x,idx_y-1,idx_z-1));
    for(it = data_idx.begin();it!=data_idx.end();it++)
    {
    	if(isFree(*it))
    	{
    		neighborPtrSets.push_back(GridNodeMap[(*it)(0)][(*it)(1)][(*it)(2)]);
    		edgeCostSets.push_back(1.414);
    	}
	}
	data_idx.clear();
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y-1,idx_z-1));
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y-1,idx_z+1));
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y+1,idx_z+1));
	data_idx.push_back(Eigen::Vector3i(idx_x+1,idx_y+1,idx_z-1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y-1,idx_z+1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y-1,idx_z-1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y+1,idx_z-1));
	data_idx.push_back(Eigen::Vector3i(idx_x-1,idx_y-1,idx_z+1));
	for(it = data_idx.begin();it!=data_idx.end();it++)
    {
    	if(isFree(*it))
    	{
    		neighborPtrSets.push_back(GridNodeMap[(*it)(0)][(*it)(1)][(*it)(2)]);
    		edgeCostSets.push_back(1.732);
    	}
	}
	data_idx.clear();
}

double AstarPathFinder::getHeu(GridNodePtr node1, GridNodePtr node2)
{
    /* 
    choose possible heuristic function you want
    Manhattan, Euclidean, Diagonal, or 0 (Dijkstra)
    Remember tie_breaker learned in lecture, add it here ?
    *
    *
    *
    STEP 1: finish the AstarPathFinder::getHeu , which is the heuristic function
    please write your code below
    *
    *
    */
    
	random_device rd;
    default_random_engine eng(rd());
    std::normal_distribution<double> distribution(0.0,1.0);

    Vector3d distance_vector = (node1->coord-node2->coord).array().abs();
    double distance = distance_vector.sum();//选用曼哈顿距离Manhattan
	//double distance = distance_vector.norm();//Euclidean
	//Diagonal
	/*
	std::ptrdiff_t max_idx, min_idx;
	double distance_max = distance_vector.maxCoeff(&max_idx);//Diagonal
	double distance_min = distance_vector.minCoeff(&min_idx);//Diagonal
	double other_value ,distance;
	for (int i=0;i<3;i++)
		if(i != max_idx && i != min_idx)
			other_value = distance_vector(i);

	distance = 1.732 * distance_min + 1.414 * (other_value-distance_min) + distance_max - other_value;
	*/
	//加入自定义tie_breaker
	//distance += 0.1*(node1->dir.cross(node2->dir)).norm() + distribution(eng)*0.1;
    return distance ;
	//return 0;
}

void AstarPathFinder::AstarGraphSearch(Vector3d start_pt, Vector3d end_pt)
{   
    ros::Time time_1 = ros::Time::now();    

    //index of start_point and end_point
    Vector3i start_idx = coord2gridIndex(start_pt);
    Vector3i end_idx   = coord2gridIndex(end_pt);
    goalIdx = end_idx;
    //position of start_point and end_point
    start_pt = gridIndex2coord(start_idx);
    end_pt   = gridIndex2coord(end_idx);

    //Initialize the pointers of struct GridNode which represent start node and goal node
    GridNodePtr startPtr = new GridNode(start_idx, start_pt);
    GridNodePtr endPtr   = new GridNode(end_idx,   end_pt);
    endPtr -> dir = goalIdx - start_idx;
    //openSet is the open_list implemented through multimap in STL library
    openSet.clear();
    // currentPtr represents the node with lowest f(n) in the open_list
    GridNodePtr currentPtr  = NULL;
    GridNodePtr neighborPtr = NULL;

    //put start node in open set
    startPtr -> gScore = 0;
    startPtr -> fScore = startPtr -> gScore + getHeu(startPtr,endPtr);   
    //STEP 1: finish the AstarPathFinder::getHeu , which is the heuristic function
    startPtr -> id = 1; //open set
    startPtr -> coord = start_pt;
    openSet.insert( make_pair(startPtr -> fScore, startPtr) );
    /*
    *
    STEP 2 :  some else preparatory works which should be done before while loop
    please write your code below
    *
    *
    */
    vector<GridNodePtr> neighborPtrSets;
    vector<double> edgeCostSets;
    std::multimap<double, GridNodePtr>::iterator it;
    double current_gScore = 0 ;

    // this is the main loop
    while ( !openSet.empty() ){
        /*
        *
        *
        step 3: Remove the node with lowest cost function from open set to closed set
        please write your code below
        
        IMPORTANT NOTE!!!
        This part you should use the C++ STL: multimap, more details can be find in Homework description
        *
        *
        */
        it = openSet.begin();
        currentPtr = it->second;
        currentPtr->id = -1;//弹出进入闭集
        openSet.erase(it);
        // if the current node is the goal 
        if( currentPtr->index == goalIdx )
        {
            ros::Time time_2 = ros::Time::now();
            terminatePtr = currentPtr;
            ROS_WARN("[A*]{sucess}  Time in A*  is %f ms, path cost if %f m", (time_2 - time_1).toSec() * 1000.0, currentPtr->gScore * resolution );            
            return;
        }
        //get the succetion
        AstarGetSucc(currentPtr, neighborPtrSets, edgeCostSets);  //STEP 4: finish AstarPathFinder::AstarGetSucc yourself     

        /*
        *
        *
        STEP 5:  For all unexpanded neigbors "m" of node "n", please finish this for loop
        please write your code below
        *        
        */         
        for(int i = 0; i < (int)neighborPtrSets.size(); i++)
        {
            /*
            *
            *
            Judge if the neigbors have been expanded
            please write your code below
            
            IMPORTANT NOTE!!!
            neighborPtrSets[i]->id = -1 : unexpanded
            neighborPtrSets[i]->id = 1 : expanded, equal to this node is in close set
            *        
            */
            neighborPtr = neighborPtrSets[i];
            current_gScore = currentPtr->gScore + edgeCostSets[i];
            if(neighborPtr -> id == 0){ //discover a new node, which is not in the closed set and open set
                /*
                *
                *
                STEP 6:  As for a new node, do what you need do ,and then put neighbor in open set and record it
                please write your code below
                *        
                */
            	neighborPtr->gScore = current_gScore;
            	neighborPtr->dir = goalIdx - neighborPtr->index;
            	neighborPtr->fScore = neighborPtr->gScore + getHeu(neighborPtr,endPtr); 
            	neighborPtr->id = 1;
            	neighborPtr->cameFrom = currentPtr;
            	openSet.insert( make_pair(neighborPtr->fScore, neighborPtr) );

                continue;
            }
            else if(neighborPtr -> id == 1){ //this node is in open set and need to judge if it needs to update, the "0" should be deleted when you are coding
                /*
                *
                *
                STEP 7:  As for a node in open set, update it , maintain the openset ,and then put neighbor in open set and record it
                please write your code below
                *        
                */
            
                if (neighborPtr->fScore > current_gScore + getHeu(neighborPtr,endPtr))
                {
                	it = openSet.find(neighborPtr->fScore);
                	for (;it != openSet.end();it++)
                	{
                		if (it->second == neighborPtr)
                		{
                			neighborPtr->gScore = current_gScore;
                			neighborPtr->fScore = current_gScore + getHeu(neighborPtr,endPtr);
                			neighborPtr->cameFrom = currentPtr;
                			openSet.insert( make_pair(neighborPtr->fScore, neighborPtr) );
                			openSet.erase(it);
                			break;
                		}

                	}

                }
                continue;
            }
            else{//this node is in closed set
                /*
                *
                please write your code below
                *        
                */
                continue;
            }
        }      
    }
    //if search fails
    ros::Time time_2 = ros::Time::now();
    if((time_2 - time_1).toSec() > 0.1)
        ROS_WARN("Time consume in Astar path finding is %f", (time_2 - time_1).toSec() );
}


vector<Vector3d> AstarPathFinder::getPath() 
{   
    vector<Vector3d> path;
    vector<GridNodePtr> gridPath;
    /*
    *
    *
    STEP 8:  trace back from the curretnt nodePtr to get all nodes along the path
    please write your code below
    *      
    */
    GridNodePtr currentPtr = terminatePtr;
    while(currentPtr)
    {
    	gridPath.push_back(currentPtr);
    	currentPtr = currentPtr->cameFrom;
    }

    for (auto ptr: gridPath)
        path.push_back(ptr->coord);
        
    reverse(path.begin(),path.end());

    return path;
}