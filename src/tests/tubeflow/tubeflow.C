
/*
 * Author
 *   David Blom, TU Delft. All rights reserved.
 */

#include <fstream>
#include <iostream>
#include <deque>
#include <string>

#include "ASMILS.H"
#include "AggressiveSpaceMapping.H"
#include "BroydenPostProcessing.H"
#include "MLIQNILSSolver.H"
#include "ManifoldMapping.H"
#include "MinIterationConvergenceMeasure.H"
#include "MultiLevelSpaceMappingSolver.H"
#include "OutputSpaceMapping.H"
#include "RelativeConvergenceMeasure.H"
#include "TubeFlowFluidSolver.H"
#include "TubeFlowSolidSolver.H"
#include "AndersonPostProcessing.H"

using namespace std;
using namespace fsi;
using namespace tubeflow;

int main()
{
    // List of solvers
    deque<std::string> fsiSolvers;

    fsiSolvers.push_back( "QN" );
    fsiSolvers.push_back( "Anderson" );
    fsiSolvers.push_back( "ML-IQN-ILS" );
    fsiSolvers.push_back( "MM" );
    fsiSolvers.push_back( "OSM" );
    fsiSolvers.push_back( "ASM" );

    // fsiSolvers.push_back( "ASM-ILS" );

    int nbTol = 7;

    bool parallelCoupling = false;
    int nbParallel = 1;

    if ( parallelCoupling )
        nbParallel = 2;

    for ( int iCoarse = 0; iCoarse < 3; iCoarse++ )
    {
        for ( int iParallel = 0; iParallel < nbParallel; iParallel++ )
        {
            for ( int iReuse = 0; iReuse < 2; iReuse++ )
            {
                for ( int nbLevels = 2; nbLevels < 3; nbLevels++ )
                {
                    for ( int iOrder = 0; iOrder < 3; iOrder++ )
                    {
                        for ( unsigned i = 0; i < fsiSolvers.size(); i++ )
                        {
                            for ( int iTol = 0; iTol < nbTol; iTol++ )
                            {
                                std::string fsiSolver = fsiSolvers.at( i );

                                // Physical settings
                                scalar r0 = 0.2;
                                scalar a0 = M_PI * r0 * r0;
                                scalar u0 = 0.1;
                                scalar p0 = 0;
                                scalar dt = 0.1;
                                scalar L = 1;
                                scalar T = 10;
                                scalar rho = 1.225;
                                scalar E = 490;
                                scalar h = 1.0e-3;
                                scalar cmk = std::sqrt( E * h / (2 * rho * r0) );

                                // scalar c0 = std::sqrt( cmk * cmk - p0 / (2 * rho) );
                                // scalar kappa = c0 / u0;
                                // scalar tau = u0 * dt / L;

                                // Computational settings
                                bool parallel;
                                scalar tol = 1.0e-6;
                                int maxIter = 100;
                                scalar initialRelaxation = 1.0e-3;
                                scalar singularityLimit = 1.0e-11;
                                int extrapolation = 2;
                                int minIter = 1;
                                int reuseInformationStartingFromTimeIndex = 0;
                                bool scaling = false;
                                scalar beta = 1;
                                bool updateJacobian = false;

                                // Grid settings
                                int N = pow( 10, 3 );
                                int couplingGridSize = N;

                                int nbReuse = 0;

                                if ( iReuse == 1 )
                                    nbReuse = 4;

                                if ( iReuse == 2 )
                                    nbReuse = 24;

                                if ( iReuse == 3 )
                                    nbReuse = 12;

                                if ( iReuse == 4 )
                                    nbReuse = 16;

                                if ( iReuse == 5 )
                                    nbReuse = 20;

                                if ( iReuse == 6 )
                                {
                                    nbReuse = 0;
                                    updateJacobian = true;
                                }

                                parallel = false;

                                if ( iParallel == 1 )
                                    parallel = true;

                                if ( (fsiSolver == "QN" || fsiSolver == "Anderson") && nbLevels == 3 )
                                    continue;

                                if ( (fsiSolver == "QN" || fsiSolver == "Anderson") && iTol > 0 )
                                    continue;

                                if ( (fsiSolver != "OSM" && iOrder == 1) || (fsiSolver != "OSM" && iOrder == 2) )
                                    continue;

                                std::string label = fsiSolver + "_" + to_string( nbReuse ) + "_" + to_string( parallel );
                                label += "_" + to_string( nbLevels ) + "_" + to_string( iCoarse );
                                label += "_iTolCoarse_" + to_string( iTol );

                                if ( fsiSolver == "OSM" )
                                    label += "_" + to_string( iOrder );

                                if ( updateJacobian )
                                    label += "_" + to_string( updateJacobian );

                                ifstream ifile( label + ".log" );

                                if ( ifile )
                                    continue;  // log file exists;

                                // Create shared pointers to solvers
                                shared_ptr<TubeFlowFluidSolver> fluid;
                                shared_ptr<TubeFlowSolidSolver> solid;
                                shared_ptr<TubeFlowFluidSolver> fineModelFluid;
                                shared_ptr<TubeFlowSolidSolver> fineModelSolid;
                                shared_ptr<MultiLevelSolver> multiLevelFluidSolver;
                                shared_ptr<MultiLevelSolver> multiLevelSolidSolver;
                                shared_ptr<MultiLevelFsiSolver> fsi;
                                shared_ptr< list<shared_ptr<ConvergenceMeasure> > > convergenceMeasures;
                                shared_ptr<MultiLevelFsiSolver> multiLevelFsiSolver;
                                shared_ptr<PostProcessing> postProcessing;
                                shared_ptr< deque<shared_ptr<ImplicitMultiLevelFsiSolver> > > models;
                                shared_ptr< deque<shared_ptr<SpaceMappingSolver> > > solvers;
                                shared_ptr<Solver> solver;
                                shared_ptr<ImplicitMultiLevelFsiSolver> fineModel;

                                models = shared_ptr< deque<shared_ptr<ImplicitMultiLevelFsiSolver> > > ( new deque<shared_ptr<ImplicitMultiLevelFsiSolver> > () );
                                solvers = shared_ptr< deque<shared_ptr<SpaceMappingSolver> > > ( new deque<shared_ptr<SpaceMappingSolver> >() );

                                if ( fsiSolver == "MM" || fsiSolver == "OSM" || fsiSolver == "ASM" || fsiSolver == "ML-IQN-ILS" || fsiSolver == "ASM-ILS" )
                                {
                                    tol = 1.0e-6;
                                    N = couplingGridSize;

                                    fineModelFluid = shared_ptr<TubeFlowFluidSolver> ( new TubeFlowFluidSolver( a0, u0, p0, dt, cmk, N, L, T, rho ) );
                                    fineModelSolid = shared_ptr<TubeFlowSolidSolver> ( new TubeFlowSolidSolver( a0, cmk, p0, rho, L, N ) );

                                    shared_ptr<RBFFunctionInterface> rbfFunction;
                                    shared_ptr<RBFInterpolation> rbfInterpolator;
                                    shared_ptr<RBFCoarsening> rbfInterpToCouplingMesh;
                                    shared_ptr<RBFCoarsening> rbfInterpToMesh;

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToCouplingMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    multiLevelFluidSolver = shared_ptr<MultiLevelSolver> ( new MultiLevelSolver( fineModelFluid, fineModelFluid, rbfInterpToCouplingMesh, rbfInterpToMesh, 0, nbLevels - 1 ) );

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToCouplingMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    multiLevelSolidSolver = shared_ptr<MultiLevelSolver> ( new MultiLevelSolver( fineModelSolid, fineModelSolid, rbfInterpToCouplingMesh, rbfInterpToMesh, 1, nbLevels - 1 ) );

                                    // Convergence measures
                                    convergenceMeasures = shared_ptr<list<shared_ptr<ConvergenceMeasure> > >( new list<shared_ptr<ConvergenceMeasure> > );

                                    convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new MinIterationConvergenceMeasure( 0, false, minIter ) ) );
                                    convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new RelativeConvergenceMeasure( 0, false, tol ) ) );

                                    if ( parallel )
                                        convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new RelativeConvergenceMeasure( 1, false, tol ) ) );

                                    multiLevelFsiSolver = shared_ptr<MultiLevelFsiSolver> ( new MultiLevelFsiSolver( multiLevelFluidSolver, multiLevelSolidSolver, convergenceMeasures, parallel, extrapolation ) );

                                    int maxUsedIterations = fineModelSolid->data.rows() * fineModelSolid->data.cols();

                                    if ( parallel )
                                        maxUsedIterations += fineModelFluid->data.rows() * fineModelFluid->data.cols();

                                    postProcessing = shared_ptr<PostProcessing> ( new AndersonPostProcessing( multiLevelFsiSolver, maxIter, initialRelaxation, maxUsedIterations, nbReuse, singularityLimit, reuseInformationStartingFromTimeIndex, scaling, beta, updateJacobian ) );

                                    fineModel = shared_ptr<ImplicitMultiLevelFsiSolver> ( new ImplicitMultiLevelFsiSolver( multiLevelFsiSolver, postProcessing ) );

                                    solid.reset();
                                    multiLevelFluidSolver.reset();
                                    multiLevelSolidSolver.reset();
                                    convergenceMeasures.reset();
                                    multiLevelFsiSolver.reset();
                                    postProcessing.reset();

                                    for ( int level = 0; level < nbLevels - 1; level++ )
                                    {
                                        if ( level == 0 && iCoarse == 0 )
                                        {
                                            tol = std::pow( 10, -iTol - 1 );
                                            N = couplingGridSize / 10;
                                        }

                                        if ( level == 0 && iCoarse == 1 )
                                        {
                                            tol = std::pow( 10, -iTol - 1 );
                                            N = couplingGridSize / 20;
                                        }

                                        if ( level == 0 && iCoarse == 2 )
                                        {
                                            tol = std::pow( 10, -iTol - 1 );
                                            N = couplingGridSize / 50;
                                        }

                                        if ( level == 1 )
                                        {
                                            tol = 1.0e-6;
                                            N = couplingGridSize;
                                        }

                                        fluid = shared_ptr<TubeFlowFluidSolver> ( new TubeFlowFluidSolver( a0, u0, p0, dt, cmk, N, L, T, rho ) );
                                        solid = shared_ptr<TubeFlowSolidSolver> ( new TubeFlowSolidSolver( a0, cmk, p0, rho, L, N ) );

                                        rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                        rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                        rbfInterpToCouplingMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                        rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                        rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                        rbfInterpToMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                        multiLevelFluidSolver = shared_ptr<MultiLevelSolver> ( new MultiLevelSolver( fluid, fineModelFluid, rbfInterpToCouplingMesh, rbfInterpToMesh, 0, level ) );

                                        rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                        rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                        rbfInterpToCouplingMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                        rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                        rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                        rbfInterpToMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                        multiLevelSolidSolver = shared_ptr<MultiLevelSolver> ( new MultiLevelSolver( solid, fineModelSolid, rbfInterpToCouplingMesh, rbfInterpToMesh, 1, level ) );

                                        // Convergence measures
                                        convergenceMeasures = shared_ptr<list<shared_ptr<ConvergenceMeasure> > >( new list<shared_ptr<ConvergenceMeasure> > );

                                        convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new MinIterationConvergenceMeasure( 0, false, minIter ) ) );
                                        convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new RelativeConvergenceMeasure( 0, false, tol ) ) );

                                        if ( parallel )
                                            convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new RelativeConvergenceMeasure( 1, false, tol ) ) );

                                        multiLevelFsiSolver = shared_ptr<MultiLevelFsiSolver> ( new MultiLevelFsiSolver( multiLevelFluidSolver, multiLevelSolidSolver, convergenceMeasures, parallel, extrapolation ) );

                                        int maxUsedIterations = solid->data.rows() * solid->data.cols();

                                        if ( parallel )
                                            maxUsedIterations += fluid->data.rows() * fluid->data.cols();

                                        postProcessing = shared_ptr<PostProcessing> ( new AndersonPostProcessing( multiLevelFsiSolver, maxIter, initialRelaxation, maxUsedIterations, nbReuse, singularityLimit, reuseInformationStartingFromTimeIndex, scaling, beta, updateJacobian ) );

                                        shared_ptr<ImplicitMultiLevelFsiSolver> implicitMultiLevelFsiSolver( new ImplicitMultiLevelFsiSolver( multiLevelFsiSolver, postProcessing ) );

                                        models->push_back( implicitMultiLevelFsiSolver );

                                        solid.reset();
                                        multiLevelFluidSolver.reset();
                                        multiLevelSolidSolver.reset();
                                        convergenceMeasures.reset();
                                        multiLevelFsiSolver.reset();
                                        postProcessing.reset();
                                        implicitMultiLevelFsiSolver.reset();
                                    }

                                    models->push_back( fineModel );

                                    assert( static_cast<int>( models->size() ) == nbLevels );

                                    if ( fsiSolver == "MM" || fsiSolver == "OSM" || fsiSolver == "ASM" || fsiSolver == "ASM-ILS" )
                                    {
                                        for ( int level = 0; level < nbLevels - 1; level++ )
                                        {
                                            shared_ptr<ImplicitMultiLevelFsiSolver> fineModel;
                                            shared_ptr<ImplicitMultiLevelFsiSolver> coarseModel;

                                            fineModel = models->at( level + 1 );

                                            if ( level == 0 )
                                                coarseModel = models->at( level );

                                            if ( level > 0 )
                                                coarseModel = solvers->at( level - 1 );

                                            shared_ptr<SpaceMapping> spaceMapping;

                                            if ( fsiSolver == "MM" )
                                                spaceMapping = shared_ptr<SpaceMapping> ( new ManifoldMapping( fineModel, coarseModel, maxIter, maxUsedIterations, nbReuse, reuseInformationStartingFromTimeIndex, singularityLimit, updateJacobian ) );

                                            if ( fsiSolver == "OSM" )
                                                spaceMapping = shared_ptr<SpaceMapping> ( new OutputSpaceMapping( fineModel, coarseModel, maxIter, maxUsedIterations, nbReuse, reuseInformationStartingFromTimeIndex, singularityLimit, iOrder ) );

                                            if ( fsiSolver == "ASM" )
                                                spaceMapping = shared_ptr<SpaceMapping> ( new AggressiveSpaceMapping( fineModel, coarseModel, maxIter, maxUsedIterations, nbReuse, reuseInformationStartingFromTimeIndex, singularityLimit ) );

                                            if ( fsiSolver == "ASM-ILS" )
                                                spaceMapping = shared_ptr<SpaceMapping> ( new ASMILS( fineModel, coarseModel, maxIter, maxUsedIterations, nbReuse, reuseInformationStartingFromTimeIndex, singularityLimit, beta ) );

                                            shared_ptr<SpaceMappingSolver > spaceMappingSolver( new SpaceMappingSolver( fineModel, coarseModel, spaceMapping ) );

                                            solvers->push_back( spaceMappingSolver );

                                            spaceMapping.reset();
                                            spaceMappingSolver.reset();
                                        }

                                        assert( static_cast<int>( solvers->size() ) == nbLevels - 1 );
                                    }
                                }

                                if ( fsiSolver == "QN" || fsiSolver == "Anderson" )
                                {
                                    assert( !fsi );

                                    tol = 1.0e-6;
                                    N = couplingGridSize;

                                    fluid = shared_ptr<TubeFlowFluidSolver> ( new TubeFlowFluidSolver( a0, u0, p0, dt, cmk, N, L, T, rho ) );
                                    solid = shared_ptr<TubeFlowSolidSolver> ( new TubeFlowSolidSolver( a0, cmk, p0, rho, L, N ) );

                                    shared_ptr<RBFFunctionInterface> rbfFunction;
                                    shared_ptr<RBFInterpolation> rbfInterpolator;
                                    shared_ptr<RBFCoarsening> rbfInterpToCouplingMesh;
                                    shared_ptr<RBFCoarsening> rbfInterpToMesh;

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToCouplingMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    shared_ptr<MultiLevelSolver> fluidSolver( new MultiLevelSolver( fluid, fluid, rbfInterpToCouplingMesh, rbfInterpToMesh, 0, 0 ) );

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToCouplingMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    rbfFunction = shared_ptr<RBFFunctionInterface>( new TPSFunction() );
                                    rbfInterpolator = shared_ptr<RBFInterpolation>( new RBFInterpolation( rbfFunction ) );
                                    rbfInterpToMesh = shared_ptr<RBFCoarsening> ( new RBFCoarsening( rbfInterpolator ) );

                                    shared_ptr<MultiLevelSolver> solidSolver( new MultiLevelSolver( solid, fluid, rbfInterpToCouplingMesh, rbfInterpToMesh, 1, 0 ) );

                                    // Convergence measures
                                    convergenceMeasures = shared_ptr<list<shared_ptr<ConvergenceMeasure> > >( new list<shared_ptr<ConvergenceMeasure> > );

                                    convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new MinIterationConvergenceMeasure( 0, false, minIter ) ) );
                                    convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new RelativeConvergenceMeasure( 0, false, tol ) ) );

                                    if ( parallel )
                                        convergenceMeasures->push_back( shared_ptr<ConvergenceMeasure> ( new RelativeConvergenceMeasure( 1, false, tol ) ) );

                                    int maxUsedIterations = solid->data.rows() * solid->data.cols();

                                    if ( parallel )
                                        maxUsedIterations += fluid->data.rows() * fluid->data.cols();

                                    fsi = shared_ptr<MultiLevelFsiSolver>( new MultiLevelFsiSolver( fluidSolver, solidSolver, convergenceMeasures, parallel, extrapolation ) );

                                    if ( fsiSolver == "Anderson" )
                                        postProcessing = shared_ptr<PostProcessing> ( new AndersonPostProcessing( fsi, maxIter, initialRelaxation, maxUsedIterations, nbReuse, singularityLimit, reuseInformationStartingFromTimeIndex, scaling, beta, updateJacobian ) );

                                    if ( fsiSolver == "QN" )
                                        postProcessing = shared_ptr<PostProcessing> ( new BroydenPostProcessing( fsi, maxIter, initialRelaxation, maxUsedIterations, nbReuse, singularityLimit, reuseInformationStartingFromTimeIndex ) );

                                    solver = shared_ptr<Solver>( new ImplicitMultiLevelFsiSolver( fsi, postProcessing ) );
                                }

                                if ( fsiSolver == "RPM" || fsiSolver == "MM" || fsiSolver == "OSM" || fsiSolver == "ASM" || fsiSolver == "ASM-ILS" )
                                    solver = shared_ptr<Solver> ( new MultiLevelSpaceMappingSolver( solvers, models, true ) );

                                if ( fsiSolver == "ML-IQN-ILS" )
                                    solver = shared_ptr<Solver> ( new MLIQNILSSolver( models, true ) );

                                try
                                {
                                    solver->run();
                                }
                                catch ( ... )
                                {
                                    continue;
                                }
                                ofstream logFile( label + ".log" );
                                assert( logFile.is_open() );

                                logFile << "label = " << label << endl;
                                logFile << "nbLevels = " << nbLevels << endl;
                                logFile << "solver = " << fsiSolver << endl;
                                logFile << "nbReuse = " << nbReuse << endl;
                                logFile << "parallel = " << parallel << endl;
                                logFile << "elapsed time = " << solver->timeElapsed() << endl;
                                logFile << "nbTimeSteps = " << fluid->timeIndex << endl;
                                logFile << "updateJacobian = " << updateJacobian << endl;
                                logFile << "iTol = " << iTol << endl;
                                logFile << "tol coarse model = " << std::pow( 10, -iTol - 1 ) << endl;

                                if ( fsiSolver == "QN" || fsiSolver == "Anderson" )
                                    logFile << "nbIter = " << fsi->nbIter << endl;

                                if ( fsiSolver == "OSM" )
                                    logFile << "order = " << iOrder << endl;

                                if ( fsiSolver == "RPM" || fsiSolver == "MM" || fsiSolver == "OSM" || fsiSolver == "ML-IQN-ILS" || fsiSolver == "ASM" || fsiSolver == "ASM-ILS" )
                                {
                                    int level = 0;

                                    for ( std::deque<shared_ptr<ImplicitMultiLevelFsiSolver> >::iterator it = models->begin(); it != models->end(); ++it )
                                    {
                                        shared_ptr<ImplicitMultiLevelFsiSolver> model = *it;
                                        logFile << "level " << level << " nbIter = " << model->fsi->nbIter << endl;
                                        logFile << "level " << level << " N = " << model->fsi->fluid->data.rows() << endl;

                                        level++;
                                    }
                                }

                                logFile.close();
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}
