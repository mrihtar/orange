#include <algorithm>
#include "table.hpp"
#include "module.hpp"
#include "errors.hpp"
#include "cls_orange.hpp"
#include "cls_example.hpp"

#include "heatmap.ppp"

#include "externs.px"

#ifdef _MSC_VER
  #pragma warning (disable : 4786 4114 4018 4267 4244)
#endif

DEFINE_TOrangeVector_classDescription(PHeatmap, "THeatmapList")

class CompareIndicesWClass {
public:
  const vector<float> &centers;
  const vector<int> &classes;

  CompareIndicesWClass(const vector<float> &acen, const vector<int> &acl)
  : centers(acen),
    classes(acl)
  {};

  bool operator() (const int &i1, const int &i2)
  { return    (classes[i1]<classes[i2])
           || ((classes[i1] == classes[i2]) && (centers[i1] < centers[i2])); }
};


class CompareIndices {
public:
  const vector<float> &centers;

  CompareIndices(const vector<float> &acen)
  : centers(acen)
  {};

  bool operator() (const int &i1, const int &i2)
  { return centers[i1] < centers[i2]; }
};


WRAPPER(ExampleTable);

#define UNKNOWN_F -1e30f

THeatmap::THeatmap(const int &h, const int &w, PExampleTable ex)
: cells(new float [h*w]),
  averages(new float [h]),
  height(h),
  width(w),
  examples(ex),
  exampleIndices(new TIntList())
{}


THeatmap::~THeatmap()
{
  delete cells;
  delete averages;
}


unsigned char *THeatmap::heatmap2string(const int &cellWidth, const int &cellHeight, const float &absLow, const float &absHigh, const float &gamma, int &size) const
{
  return bitmap2string(cellWidth, cellHeight, size, cells, width, height, absLow, absHigh, gamma);
}

unsigned char *THeatmap::averages2string(const int &cellWidth, const int &cellHeight, const float &absLow, const float &absHigh, const float &gamma, int &size) const
{
  return bitmap2string(cellWidth, cellHeight, size, averages, 1, height, absLow, absHigh, gamma);
}


float THeatmap::getCellIntensity(const int &y, const int &x) const
{ 
  if ((y<0) || (y>=height))
    raiseError("row index out of range");
  if ((x<0) || (y>=height))
    raiseError("column index out of range");

  return cells[y*width+x];
}


float THeatmap::getRowIntensity(const int &y) const
{ 
  if ((y<0) || (y>=height))
    raiseError("row index out of range");

  return averages[y];
}


/* Expands the bitmap 
   Each pixel in bitmap 'smmp' is replaced by a square with
     given 'cellWidth' and 'cellHeight'
   The original bitmaps width and height are given by arguments
     'width' and 'height'

   Beside returning the bitmap, the function return its size
   in bytes (argument '&size'). Due to alignment of rows to 4 bytes,
   this does not necessarily equal cellWidth * cellHeight * width * height.
*/

unsigned char *bitmap2string(const int &cellWidth, const int &cellHeight, int &size, float *intensity, const int &width, const int &height, const float &absLow, const float &absHigh, const float &gamma)
{
  const int lineWidth = width * cellWidth;
  const int fill = (4 - lineWidth & 3) & 3;
  const int rowSize = lineWidth + fill;
  size = rowSize * height * cellHeight;

  unsigned char *res = new unsigned char[size];
  unsigned char *resi = res;

  if (gamma == 1.0) {
    const float colorFact = 249.0/(absHigh - absLow);

    for(int line = 0; line<height; line++) {
      unsigned char *thisline = resi;
      int xpoints;
      for(xpoints = width; xpoints--; intensity++) {
        unsigned char col;
        if (*intensity == UNKNOWN_F)
          col = 255;
        else if (*intensity < absLow)
          col = 253;
        else if (*intensity > absHigh)
          col = 254;
        else
          col = int(floor(colorFact * (*intensity - absLow)));

        for(int inpoints = cellWidth; inpoints--; *(resi++) = col);
      }

      resi += fill;
      for(xpoints = cellHeight-1; xpoints--; resi += rowSize)
        memcpy(resi, thisline, lineWidth);
    }
  }
  else {
    const float colorBase = (absLow + absHigh) / 2;
    const float colorFact = 2 / (absHigh - absLow);

    for(int line = 0; line<height; line++) {
      unsigned char *thisline = resi;
      int xpoints;
      for(xpoints = width; xpoints--; intensity++) {
        unsigned char col;

        if (*intensity == UNKNOWN_F)
          col = 255;
        else if (*intensity < absLow)
          col = 253;
        else if (*intensity > absHigh)
          col = 254;

        float norm = colorFact * (*intensity - colorBase);
        if ((norm > -0.008) && (norm < 0.008))
          norm = 125;
        else
          norm = 124.5 * (1 + (norm<0 ? -exp(gamma * log(-norm)) : exp(gamma * log(norm))));

        if (norm<0)
          col = 0;
        else if (norm>249)
          col = 249;
        else
          col = int(floor(norm));

        for(int inpoints = cellWidth; inpoints--; *(resi++) = col);
      }

      resi += fill;
      for(xpoints = cellHeight-1; xpoints--; resi += rowSize)
        memcpy(resi, thisline, lineWidth);
    }
  }

  return res;
}


THeatmapConstructor::THeatmapConstructor(PExampleTable table, PHeatmapConstructor baseHeatmap, bool noSorting, bool disregardClass)
: sortedExamples(new TExampleTable(table, 1)), // lock, but do not copy
  floatMap(),
  classBoundaries(),
  lineCenters(),
  nColumns(table->domain->attributes->size()),
  nRows(table->numberOfExamples()),
  nClasses(0)
{
  TExampleTable &etable = table.getReference();
  if (baseHeatmap && (etable.numberOfExamples() != baseHeatmap->sortedExamples->numberOfExamples()))
    raiseError("'baseHeatmap has a different number of spots");

  TExampleTable &esorted = sortedExamples.getReference();

  bool haveBase = baseHeatmap;

  PITERATE(TVarList, ai, etable.domain->attributes)
    if ((*ai)->varType != TValue::FLOATVAR)
      raiseError("data contains a discrete attribute '%s'", (*ai)->name.c_str());

  if (etable.domain->classVar && !disregardClass) {
    if (etable.domain->classVar->varType != TValue::INTVAR)
      raiseError("class attribute is not discrete");
    nClasses = etable.domain->classVar->noOfValues();
  }
  else
    nClasses = 0;

  vector<float *> tempFloatMap;
  vector<float> tempLineCenters;
  vector<float> tempLineAverages;
  vector<int>classes;

  tempFloatMap.reserve(nRows);

  tempLineCenters.reserve(nRows);
  if (!haveBase)
    sortIndices.reserve(nRows);

  try {
    // Extract the data from the table, compute the centers and fill the sortIndices
    EITERATE(ei, etable) {
      if (!haveBase)
        sortIndices.push_back(sortIndices.size());

      float *i_floatMap = new float[nColumns];
      tempFloatMap.push_back(i_floatMap);

      if (nClasses) {
        TValue &classVal = (*ei).getClass();
        classes.push_back(classVal.isSpecial() ? 999 : classVal.intV);
      }
      
      TExample::const_iterator eii((*ei).begin());

      float sumBri = 0.0;
      float sumBriX = 0.0;
      int sumX = 0;
      int N = 0;
      float thismax = -1e30f;
      float thismin = 1e30f;
      
      float *rai = i_floatMap;
      for(int xpoint = 0; xpoint<nColumns; rai++, eii++, xpoint++) {
        if ((*eii).isSpecial()) {
          *rai = UNKNOWN_F;
        }
        else {
          *rai = (*eii).floatV;
          sumBri += *rai;
          sumBriX += *rai * xpoint;
          sumX += xpoint;
          N += 1;
          if (*rai > thismax)
            thismax = *rai;
          if (*rai < thismin)
            thismin = *rai;
        }
      }

      tempLineAverages.push_back(N ? sumBri/N : UNKNOWN_F);
      tempLineCenters.push_back(N && (thismax != thismin) ? (sumBriX - thismin * sumX) / (sumBri - thismin * N) : UNKNOWN_F);
    }

    if (haveBase) {
      sortIndices = baseHeatmap->sortIndices;
      classBoundaries = baseHeatmap->classBoundaries;
    }

    else if (!noSorting) {
      if (nClasses) {
        CompareIndicesWClass compare(tempLineCenters, classes);
        sort(sortIndices.begin(), sortIndices.end(), compare);
      }
      else {
        CompareIndices compare(tempLineCenters);
        sort(sortIndices.begin(), sortIndices.end(), compare);
      }
    }

    floatMap.reserve(nRows);
    lineCenters.reserve(nRows);
    lineAverages.reserve(nRows);
        
    int pcl = -1;
    if (!haveBase && nClasses)
      while(pcl < classes.front()) {
        classBoundaries.push_back(0);
        pcl++;
      }

    ITERATE(vector<int>, si, sortIndices) {
      esorted.addExample(etable[*si]);
      lineCenters.push_back(tempLineCenters[*si]);
      lineAverages.push_back(tempLineAverages[*si]);
      floatMap.push_back(tempFloatMap[*si]);
      tempFloatMap[*si] = NULL;

      if (!haveBase && nClasses)
        while(pcl < classes[*si]) {
          classBoundaries.push_back(floatMap.size());
          pcl++;
        }
    }

    if (!haveBase) {
      if (!nClasses)
        classBoundaries.push_back(0);
      for(; pcl<nClasses; pcl++)
        classBoundaries.push_back(floatMap.size());
    }
  }
  catch (...) {
    ITERATE(vector<float *>, tfmi, tempFloatMap)
      delete *tfmi;
    ITERATE(vector<float *>, fmi, floatMap)
      delete *fmi;
    throw;
  }
}


THeatmapConstructor::~THeatmapConstructor()
{
  ITERATE(vector<float *>, fmi, floatMap)
    delete *fmi;
}


PHeatmapList THeatmapConstructor::operator ()(const float &unadjustedSqueeze, float &abslow, float &abshigh)
{
  abshigh = -1e30f;
  abslow = 1e30f;
    
  PHeatmapList hml = new THeatmapList;

  int *spec = new int[nColumns];
  
  for(int classNo = 0, ncl = nClasses ? nClasses : 1; classNo < ncl; classNo++) {
    const int classBegin = classBoundaries[classNo];
    const int classEnd = classBoundaries[classNo+1];

    if (classBegin == classEnd) {
      THeatmap *hm = new THeatmap(0, nColumns, sortedExamples);
      hml->push_back(hm);
      hm->exampleIndices->push_back(classBegin);
      hm->exampleIndices->push_back(classBegin);
      continue;
    }

    int nLines = int(floor(0.5 + (classEnd - classBegin) * unadjustedSqueeze));
    const float squeeze = float(nLines) / (classEnd-classBegin);

    THeatmap *hm = new THeatmap(nLines, nColumns, sortedExamples);
    hml->push_back(hm);

    float *fmi = hm->cells;
    float *ami = hm->averages;

    float inThisRow = 0;
    float *ri, *fri;
    int *si;
    int xpoint;
    vector<float>::const_iterator lavi(lineAverages.begin());

    int exampleIndex = classBegin;
    hm->exampleIndices->push_back(exampleIndex);

    for(vector<float *>::iterator rowi = floatMap.begin()+classBegin, rowe = floatMap.begin()+classEnd; rowi!=rowe; nLines--, inThisRow-=1.0, ami++, fmi+=nColumns) {
      for(xpoint = nColumns, ri = fmi, si = spec; xpoint--; *(ri++) = 0.0, *(si++) = 0);
      *ami = 0.0;
      int nDefinedAverages = 0;

      for(; (rowi != rowe) && ((inThisRow < 1.0) || (nLines==1)); inThisRow += squeeze, rowi++, exampleIndex++, lavi++) {
        for(xpoint = nColumns, fri = *rowi, ri = fmi, si = spec; xpoint--; fri++, ri++, si++)
          if (*fri != UNKNOWN_F) {
            *ri += *fri;
            (*si)++;
          }

        if (*lavi != UNKNOWN_F) {
          *ami += *lavi;
          nDefinedAverages++;
        }
      }

      hm->exampleIndices->push_back(exampleIndex);
      for(xpoint = nColumns, si = spec, ri = fmi; xpoint--; ri++, si++) {
        if (*si) {
          *fmi = *fmi / *si;
          if (*fmi < abslow)
            abslow = *fmi;
          if (*fmi > abshigh)
            abshigh = *fmi;
        }
        else
          *fmi = UNKNOWN_F;
      }

      *ami = nDefinedAverages ? *ami/nDefinedAverages : UNKNOWN_F;
    }
  }

  delete spec;

  return hml;
}


unsigned char *THeatmapConstructor::getLegend(const int &width, const int &height, const float &gamma, int &size) const
{
  float *fmp = new float[width], *fmpi = fmp;

  float wi1 = width-1;
  for(int wi = 0; wi<width; *(fmpi++) = (wi++)/wi1);
  
  unsigned char *legend = bitmap2string(1, height, size, fmp, width, 1, 0, 1, gamma);
  delete fmp;
  return legend;
}
