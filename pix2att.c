/*
**
** The MIT License (MIT)
**
** Copyright (c) 2015 Kyle Shannon <kyle at pobox dot com>
** 
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
** 
** The above copyright notice and this permission notice shall be included in all
** copies or substantial portions of the Software.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
** SOFTWARE.
*/

#include <assert.h>
#include <stdlib.h>

#include "gdal.h"

void Usage()
{
    printf( "pointpixel [-b band] [-p] [-gt n] raster point layer attribute\n" );
}

static int TransformGeoToPixelSpace( double *adfInvGeoTransform, double dfX,
                                     double dfY, int *iPixel, int *iLine )
{
    *iPixel = (int) floor( adfInvGeoTransform[0] +
                           adfInvGeoTransform[1] * dfX +
                           adfInvGeoTransform[2] * dfY );
    *iLine  = (int) floor( adfInvGeoTransform[3] +
                           adfInvGeoTransform[4] * dfX +
                           adfInvGeoTransform[5] * dfY );
    return 0;
}

int main( int argc, char *argv[] )
{
    GDALDatasetH hRasterDS, hVectorDS;
    GDALRasterBandH hBand;
    OGRLayerH hLayer;
    OGRFeatureDefnH hFeatDefn;
    OGRFeatureH hFeature;
    OGRFieldDefnH hFieldDefn;
    OGRGeometryH hGeometry;
    GDALProgressFunc pfnProgress = GDALDummyProgress;

    OGRSpatialReferenceH hRasterSRS, hVectorSRS;
    OGRCoordinateTransformationH hCt;
    int bTransform = FALSE;
    GIntBig *nFids;

    int nTransactions = 1;

    double dfVal;
    int nVal;
    GDALDataType eType;

    const char *pszRaster = NULL;
    const char *pszVector = NULL;
    int nBand = 1;
    const char *pszLayer = NULL;
    const char *pszAtt = NULL;
    double adfGeoTransform[6], adfInvGeoTransform[6];
    double dfX, dfY, dfZ;
    int iPixel, iLine;
    int iField;
    int i = 1;
    int n, rc;
    while( i < argc )
    {
        if( EQUAL( argv[i], "-b" ) )
            nBand = atoi( argv[++i] );
        if( EQUAL( argv[i], "-p" ) )
            pfnProgress = GDALTermProgress;
        if( EQUAL( argv[i], "-gt" ) )
            nTransactions = atoi( argv[++i] );
        else if( pszRaster == NULL )
            pszRaster = argv[i];
        else if( pszVector == NULL )
            pszVector = argv[i];
        else if( pszLayer == NULL )
            pszLayer = argv[i];
        else if( pszAtt == NULL )
            pszAtt = argv[i];
        i++;
    }
    if( !pszRaster || !pszVector || !pszLayer || !pszAtt )
    {
        printf( "Invalid input.\n" );
        exit( 1 );
    }
    GDALAllRegister();
    hRasterDS = GDALOpenEx( pszRaster, GDAL_OF_RASTER | GDAL_OF_READONLY,
                            NULL, NULL, NULL );
    hVectorDS = GDALOpenEx( pszVector, GDAL_OF_VECTOR | GDAL_OF_UPDATE,
                           NULL, NULL, NULL ); 
    if( !hRasterDS || !hVectorDS )
    {
        printf( "Failed to open a dataset.\n" );
        return 1;
    }

    rc = GDALGetGeoTransform( hRasterDS, adfGeoTransform );
    assert( rc == 0 );
    rc = GDALInvGeoTransform( adfGeoTransform, adfInvGeoTransform );
    assert( rc == 1 );
    hBand = GDALGetRasterBand( hRasterDS, nBand );
    hLayer = OGR_DS_GetLayerByName( hVectorDS, pszLayer );
    if( !hLayer )
        exit( 1 );

    eType = GDALGetRasterDataType( hBand );
    if( eType >= GDT_Float32 )
        hFieldDefn = OGR_Fld_Create( pszAtt, OFTReal );
    else
        hFieldDefn = OGR_Fld_Create( pszAtt, OFTInteger );

    if( OGR_L_CreateField( hLayer, hFieldDefn, TRUE ) != OGRERR_NONE )
    {
        printf( "Creating Name field failed.\n" );
        exit( 1 );
    }
    OGR_Fld_Destroy( hFieldDefn );
    hFeatDefn = OGR_L_GetLayerDefn( hLayer );
    iField = OGR_FD_GetFieldIndex( hFeatDefn, pszAtt );

    /* Setup coordinate transformation if needed */
    hRasterSRS = OSRNewSpatialReference( GDALGetProjectionRef( hRasterDS ) );
    hVectorSRS = OGR_L_GetSpatialRef( hLayer );
    bTransform = !OSRIsSame( hRasterDS, hVectorSRS );

    if( bTransform )
        hCt = OCTNewCoordinateTransformation( hVectorSRS, hRasterSRS );
    else
        hCt = NULL;

    /* TODO: Check geometry type */

    n = OGR_L_GetFeatureCount( hLayer, TRUE );
    OGR_L_ResetReading( hLayer );

    nFids = CPLMalloc( sizeof( GIntBig ) * n );
    i = 0;
    while( (hFeature = OGR_L_GetNextFeature( hLayer ) ) != NULL )
    {
        nFids[i++] = OGR_F_GetFID( hFeature );
        OGR_F_Destroy( hFeature );
    }
    pfnProgress( 0.0, NULL, NULL );
    for( i = 0; i < n; i++ )
    {
        hFeature = OGR_L_GetFeature( hLayer, nFids[i] );
        hGeometry = OGR_F_GetGeometryRef( hFeature );
        dfX = OGR_G_GetX( hGeometry, 0 );
        dfY = OGR_G_GetY( hGeometry, 0 );
        if( bTransform )
            OCTTransform( hCt, 1, &dfX, &dfY, &dfZ );
        TransformGeoToPixelSpace( adfInvGeoTransform, dfX, dfY, &iPixel, &iLine );
        if( eType >= GDT_Float32 )
        {
            GDALRasterIO( hBand, GF_Read, iPixel, iLine, 1, 1,
                          &dfVal, 1, 1, GDT_Float64, 0, 0 );

            OGR_F_SetFieldDouble( hFeature, iField, dfVal );
        }
        else
        {
            GDALRasterIO( hBand, GF_Read, iPixel, iLine, 1, 1,
                          &nVal, 1, 1, GDT_Int32, 0, 0 );
            OGR_F_SetFieldInteger( hFeature, iField, nVal );
        }
        OGR_L_SetFeature( hLayer, hFeature );
        OGR_F_Destroy( hFeature );
        pfnProgress( (float)i / (float)n, NULL, NULL );
    }
    pfnProgress( 1.0, NULL, NULL );

    OSRDestroySpatialReference( hRasterSRS );
    OCTDestroyCoordinateTransformation( hCt );
    GDALClose( hRasterDS );
    GDALClose( hVectorDS );

    return 0;
}

